/*
 * gtkoffscreenbox.c
 */

#include "config.h"

#include <math.h>
#include <gtk/gtk.h>

#include "gtkoffscreenbox.h"

static void        gtk_offscreen_box_realize       (GtkWidget       *widget);
static void        gtk_offscreen_box_unrealize     (GtkWidget       *widget);
static void        gtk_offscreen_box_size_request  (GtkWidget       *widget,
                                                    GtkRequisition  *requisition);
static void        gtk_offscreen_box_size_allocate (GtkWidget       *widget,
                                                    GtkAllocation   *allocation);
static gboolean    gtk_offscreen_box_damage        (GtkWidget       *widget,
                                                    GdkEventExpose  *event);
static gboolean    gtk_offscreen_box_expose        (GtkWidget       *widget,
                                                    GdkEventExpose  *offscreen);

static void        gtk_offscreen_box_add           (GtkContainer    *container,
                                                    GtkWidget       *child);
static void        gtk_offscreen_box_remove        (GtkContainer    *container,
                                                    GtkWidget       *widget);
static void        gtk_offscreen_box_forall        (GtkContainer    *container,
                                                    gboolean         include_internals,
                                                    GtkCallback      callback,
                                                    gpointer         callback_data);
static GType       gtk_offscreen_box_child_type    (GtkContainer    *container);

static void        from_parent     (GdkWindow       *child,
				    gdouble          parent_x,
				    gdouble          parent_y,
				    gdouble         *child_x,
				    gdouble         *child_y);
static void        to_parent       (GdkWindow       *child,
				    gdouble          child_x,
				    gdouble          child_y,
				    gdouble         *parent_x,
				    gdouble         *parent_y);

static const GdkOffscreenChildHooks offscreen_hooks = {
  from_parent,
  to_parent,
};

#define CHILD1_SIZE_SCALE 1.0
#define CHILD2_SIZE_SCALE 1.0

G_DEFINE_TYPE (GtkOffscreenBox, gtk_offscreen_box, GTK_TYPE_CONTAINER);

static void
gtk_offscreen_box_class_init (GtkOffscreenBoxClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  widget_class->realize = gtk_offscreen_box_realize;
  widget_class->unrealize = gtk_offscreen_box_unrealize;
  widget_class->size_request = gtk_offscreen_box_size_request;
  widget_class->size_allocate = gtk_offscreen_box_size_allocate;
  widget_class->expose_event = gtk_offscreen_box_expose;

  g_signal_override_class_closure (g_signal_lookup ("damage-event", GTK_TYPE_WIDGET),
                                   GTK_TYPE_OFFSCREEN_BOX,
                                   g_cclosure_new (G_CALLBACK (gtk_offscreen_box_damage),
                                                   NULL, NULL));

  container_class->add = gtk_offscreen_box_add;
  container_class->remove = gtk_offscreen_box_remove;
  container_class->forall = gtk_offscreen_box_forall;
  container_class->child_type = gtk_offscreen_box_child_type;
}

static void
gtk_offscreen_box_init (GtkOffscreenBox *offscreen_box)
{
  GTK_WIDGET_UNSET_FLAGS (offscreen_box, GTK_NO_WINDOW);
}

GtkWidget *
gtk_offscreen_box_new (void)
{
  return g_object_new (GTK_TYPE_OFFSCREEN_BOX, NULL);
}

static void
gtk_offscreen_box_realize (GtkWidget *widget)
{
  GtkOffscreenBox *offscreen_box = GTK_OFFSCREEN_BOX (widget);
  GdkWindowAttr attributes;
  gint attributes_mask;
  gint border_width;
  GtkRequisition child_requisition;
  int start_y = 0;

  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

  border_width = GTK_CONTAINER (widget)->border_width;

  attributes.x = widget->allocation.x + border_width;
  attributes.y = widget->allocation.y + border_width;
  attributes.width = widget->allocation.width - 2 * border_width;
  attributes.height = widget->allocation.height - 2 * border_width;
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.event_mask = gtk_widget_get_events (widget)
			| GDK_EXPOSURE_MASK
			| GDK_POINTER_MOTION_MASK
			| GDK_BUTTON_PRESS_MASK
			| GDK_BUTTON_RELEASE_MASK
			| GDK_SCROLL_MASK
			| GDK_ENTER_NOTIFY_MASK
			| GDK_LEAVE_NOTIFY_MASK;

  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);
  attributes.wclass = GDK_INPUT_OUTPUT;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget),
				   &attributes, attributes_mask);
  gdk_window_set_user_data (widget->window, widget);

  attributes.window_type = GDK_WINDOW_OFFSCREEN;
  
  /* Child 1 */
  attributes.x = attributes.y = 0;
  if (offscreen_box->child1 && GTK_WIDGET_VISIBLE (offscreen_box->child1))
    {
      attributes.width = offscreen_box->child1->allocation.width;
      attributes.height = offscreen_box->child1->allocation.height;
      start_y += offscreen_box->child1->allocation.height;
    }
  offscreen_box->offscreen_window1 = gdk_window_new (widget->window,
						     &attributes, attributes_mask);
  gdk_window_set_user_data (offscreen_box->offscreen_window1, widget);
  if (offscreen_box->child1)
    gtk_widget_set_parent_window (offscreen_box->child1, offscreen_box->offscreen_window1);

  /* Child 2 */
  attributes.y = start_y;
  child_requisition.width = child_requisition.height = 0;
  if (offscreen_box->child2 && GTK_WIDGET_VISIBLE (offscreen_box->child2))
    {
      attributes.width = offscreen_box->child2->allocation.width;
      attributes.height = offscreen_box->child2->allocation.height;
    }
  offscreen_box->offscreen_window2 = gdk_window_new (widget->window,
						     &attributes, attributes_mask);
  gdk_window_set_offscreen_hooks (offscreen_box->offscreen_window2, &offscreen_hooks);
  gdk_window_set_user_data (offscreen_box->offscreen_window2, widget);
  if (offscreen_box->child2)
    gtk_widget_set_parent_window (offscreen_box->child2, offscreen_box->offscreen_window2);

  widget->style = gtk_style_attach (widget->style, widget->window);

  gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);
  gtk_style_set_background (widget->style, offscreen_box->offscreen_window1, GTK_STATE_NORMAL);
  gtk_style_set_background (widget->style, offscreen_box->offscreen_window2, GTK_STATE_NORMAL);

  gdk_window_show (offscreen_box->offscreen_window1);
  gdk_window_show (offscreen_box->offscreen_window2);
}

static void
gtk_offscreen_box_unrealize (GtkWidget *widget)
{
  GtkOffscreenBox *offscreen_box = GTK_OFFSCREEN_BOX (widget);

  gdk_window_set_user_data (offscreen_box->offscreen_window1, NULL);
  gdk_window_destroy (offscreen_box->offscreen_window1);
  offscreen_box->offscreen_window1 = NULL;

  gdk_window_set_user_data (offscreen_box->offscreen_window2, NULL);
  gdk_window_destroy (offscreen_box->offscreen_window2);
  offscreen_box->offscreen_window2 = NULL;

  GTK_WIDGET_CLASS (gtk_offscreen_box_parent_class)->unrealize (widget);
}

static GType
gtk_offscreen_box_child_type (GtkContainer *container)
{
  GtkOffscreenBox *offscreen_box = GTK_OFFSCREEN_BOX (container);

  if (offscreen_box->child1 && offscreen_box->child2)
    return G_TYPE_NONE;

  return GTK_TYPE_WIDGET;
}

static void
gtk_offscreen_box_add (GtkContainer *container,
		       GtkWidget    *widget)
{
  GtkOffscreenBox *offscreen_box = GTK_OFFSCREEN_BOX (container);

  if (!offscreen_box->child1)
    gtk_offscreen_box_add1 (offscreen_box, widget);
  else if (!offscreen_box->child2)
    gtk_offscreen_box_add2 (offscreen_box, widget);
  else
    g_warning ("GtkOffscreenBox cannot have more than 2 children\n");
}

void
gtk_offscreen_box_add1 (GtkOffscreenBox *offscreen_box,
			GtkWidget       *child)
{
  g_return_if_fail (GTK_IS_OFFSCREEN_BOX (offscreen_box));
  g_return_if_fail (GTK_IS_WIDGET (child));

  if (offscreen_box->child1 == NULL)
    {
      gtk_widget_set_parent_window (child, offscreen_box->offscreen_window1);
      gtk_widget_set_parent (child, GTK_WIDGET (offscreen_box));
      offscreen_box->child1 = child;
    }
}

void
gtk_offscreen_box_add2 (GtkOffscreenBox  *offscreen_box,
			GtkWidget    *child)
{
  g_return_if_fail (GTK_IS_OFFSCREEN_BOX (offscreen_box));
  g_return_if_fail (GTK_IS_WIDGET (child));

  if (offscreen_box->child2 == NULL)
    {
      gtk_widget_set_parent_window (child, offscreen_box->offscreen_window2);
      gtk_widget_set_parent (child, GTK_WIDGET (offscreen_box));
      offscreen_box->child2 = child;
    }
}

static void
gtk_offscreen_box_remove (GtkContainer *container,
			  GtkWidget    *widget)
{
  GtkOffscreenBox *offscreen_box = GTK_OFFSCREEN_BOX (container);
  gboolean was_visible;

  was_visible = GTK_WIDGET_VISIBLE (widget);

  if (offscreen_box->child1 == widget)
    {
      gtk_widget_unparent (widget);

      offscreen_box->child1 = NULL;

      if (was_visible && GTK_WIDGET_VISIBLE (container))
	gtk_widget_queue_resize (GTK_WIDGET (container));
    }
  else if (offscreen_box->child2 == widget)
    {
      gtk_widget_unparent (widget);

      offscreen_box->child2 = NULL;

      if (was_visible && GTK_WIDGET_VISIBLE (container))
	gtk_widget_queue_resize (GTK_WIDGET (container));
    }
}

static void
gtk_offscreen_box_forall (GtkContainer *container,
			  gboolean      include_internals,
			  GtkCallback   callback,
			  gpointer      callback_data)
{
  GtkOffscreenBox *offscreen_box = GTK_OFFSCREEN_BOX (container);

  g_return_if_fail (callback != NULL);

  if (offscreen_box->child1)
    (*callback) (offscreen_box->child1, callback_data);
  if (offscreen_box->child2)
    (*callback) (offscreen_box->child2, callback_data);
}

void
gtk_offscreen_box_set_angle (GtkOffscreenBox  *offscreen_box,
			     gdouble           angle)
{
  g_return_if_fail (GTK_IS_OFFSCREEN_BOX (offscreen_box));

  offscreen_box->angle = angle;
  gtk_widget_queue_draw (GTK_WIDGET (offscreen_box));

  /* TODO: Really needs to resent pointer events if over the rotated window */
}


static void
gtk_offscreen_box_size_request (GtkWidget      *widget,
				GtkRequisition *requisition)
{
  GtkOffscreenBox *offscreen_box = GTK_OFFSCREEN_BOX (widget);
  int w, h;

  w = 0;
  h = 0;

  if (offscreen_box->child1 && GTK_WIDGET_VISIBLE (offscreen_box->child1))
    {
      GtkRequisition child_requisition;

      gtk_widget_size_request (offscreen_box->child1, &child_requisition);

      w = MAX (w, CHILD1_SIZE_SCALE * child_requisition.width);
      h += CHILD1_SIZE_SCALE * child_requisition.height;
    }

  if (offscreen_box->child2 && GTK_WIDGET_VISIBLE (offscreen_box->child2))
    {
      GtkRequisition child_requisition;

      gtk_widget_size_request (offscreen_box->child2, &child_requisition);

      w = MAX (w, CHILD2_SIZE_SCALE * child_requisition.width);
      h += CHILD2_SIZE_SCALE * child_requisition.height;
    }

  requisition->width = GTK_CONTAINER (widget)->border_width * 2 + w;
  requisition->height = GTK_CONTAINER (widget)->border_width * 2 + h;
}

static void
gtk_offscreen_box_size_allocate (GtkWidget     *widget,
				 GtkAllocation *allocation)
{
  GtkOffscreenBox *offscreen_box;
  gint border_width;
  gint start_y;

  widget->allocation = *allocation;
  offscreen_box = GTK_OFFSCREEN_BOX (widget);

  border_width = GTK_CONTAINER (widget)->border_width;

  if (GTK_WIDGET_REALIZED (widget))
    gdk_window_move_resize (widget->window,
                            allocation->x + border_width,
                            allocation->y + border_width,
                            allocation->width - border_width * 2,
                            allocation->height - border_width * 2);

  start_y = 0;

  if (offscreen_box->child1 && GTK_WIDGET_VISIBLE (offscreen_box->child1))
    {
      GtkRequisition child_requisition;
      GtkAllocation child_allocation;

      gtk_widget_get_child_requisition (offscreen_box->child1, &child_requisition);
      child_allocation.x = child_requisition.width * (CHILD1_SIZE_SCALE - 1.0) / 2;
      child_allocation.y = start_y + child_requisition.height * (CHILD1_SIZE_SCALE - 1.0) / 2;
      child_allocation.width = MAX (1, (gint) widget->allocation.width - 2 * border_width);
      child_allocation.height = child_requisition.height;

      start_y += CHILD1_SIZE_SCALE * child_requisition.height;

      if (GTK_WIDGET_REALIZED (widget))
	gdk_window_move_resize (offscreen_box->offscreen_window1,
				child_allocation.x,
                                child_allocation.y,
				child_allocation.width,
                                child_allocation.height);

      child_allocation.x = child_allocation.y = 0;
      gtk_widget_size_allocate (offscreen_box->child1, &child_allocation);
    }

  if (offscreen_box->child2 && GTK_WIDGET_VISIBLE (offscreen_box->child2))
    {
      GtkRequisition child_requisition;
      GtkAllocation child_allocation;

      gtk_widget_get_child_requisition (offscreen_box->child2, &child_requisition);
      child_allocation.x = child_requisition.width * (CHILD2_SIZE_SCALE - 1.0) / 2;
      child_allocation.y = start_y + child_requisition.height * (CHILD2_SIZE_SCALE - 1.0) / 2;
      child_allocation.width = MAX (1, (gint) widget->allocation.width - 2 * border_width);
      child_allocation.height = child_requisition.height;

      start_y += CHILD2_SIZE_SCALE * child_requisition.height;

      if (GTK_WIDGET_REALIZED (widget))
	gdk_window_move_resize (offscreen_box->offscreen_window2,
				child_allocation.x,
                                child_allocation.y,
				child_allocation.width,
                                child_allocation.height);

      child_allocation.x = child_allocation.y = 0;
      gtk_widget_size_allocate (offscreen_box->child2, &child_allocation);
    }
}

static gboolean
gtk_offscreen_box_damage (GtkWidget      *widget,
                          GdkEventExpose *event)
{
  gdk_window_invalidate_rect (widget->window, NULL, FALSE);

  return TRUE;
}

static gboolean
gtk_offscreen_box_expose (GtkWidget      *widget,
			  GdkEventExpose *event)
{
  GtkOffscreenBox *offscreen_box = GTK_OFFSCREEN_BOX (widget);

  if (GTK_WIDGET_DRAWABLE (widget))
    {
      if (event->window == widget->window)
	{
          GdkPixmap *pixmap;
          GtkAllocation child_area;
          cairo_t *cr;
	  int start_y = 0;

	  if (offscreen_box->child1 && GTK_WIDGET_VISIBLE (offscreen_box->child1))
	    {
	      pixmap = gdk_window_get_offscreen_pixmap (offscreen_box->offscreen_window1);
              child_area = offscreen_box->child1->allocation;

	      cr = gdk_cairo_create (widget->window);

              gdk_cairo_set_source_pixmap (cr, pixmap, 0, 0);
              cairo_paint (cr);

              cairo_destroy (cr);

              start_y += child_area.height;
	    }

	  if (offscreen_box->child2 && GTK_WIDGET_VISIBLE (offscreen_box->child2))
	    {
              gint w, h;

	      pixmap = gdk_window_get_offscreen_pixmap (offscreen_box->offscreen_window2);
              child_area = offscreen_box->child2->allocation;

	      cr = gdk_cairo_create (widget->window);

              /* transform */
	      cairo_translate (cr, 0, start_y);
	      cairo_translate (cr, child_area.width / 2, child_area.height / 2);
	      cairo_rotate (cr, offscreen_box->angle);
	      cairo_translate (cr, -child_area.width / 2, -child_area.height / 2);

              /* clip */
              gdk_drawable_get_size (pixmap, &w, &h);
              cairo_rectangle (cr, 0, 0, w, h);
              cairo_clip (cr);

              /* paint */
	      gdk_cairo_set_source_pixmap (cr, pixmap, 0, 0);
	      cairo_paint (cr);

              cairo_destroy (cr);
	    }
	}
      else if (event->window == offscreen_box->offscreen_window1)
	{
	  gtk_paint_flat_box (widget->style, event->window,
			      GTK_STATE_NORMAL, GTK_SHADOW_NONE,
			      &event->area, widget, "blah",
			      0, 0, -1, -1);

	  if (offscreen_box->child1)
	    gtk_container_propagate_expose (GTK_CONTAINER (widget),
					    offscreen_box->child1,
                                            event);
	}
      else if (event->window == offscreen_box->offscreen_window2)
	{
	  gtk_paint_flat_box (widget->style, event->window,
			      GTK_STATE_NORMAL, GTK_SHADOW_NONE,
			      &event->area, widget, "blah",
			      0, 0, -1, -1);

	  if (offscreen_box->child2)
	    gtk_container_propagate_expose (GTK_CONTAINER (widget),
					    offscreen_box->child2,
                                            event);
	}
    }

  return FALSE;
}

static void
from_parent (GdkWindow *child,
	     gdouble    parent_x,
	     gdouble    parent_y,
	     gdouble   *child_x,
	     gdouble   *child_y)
{
  GtkOffscreenBox *offscreen_box = NULL;
  GtkAllocation child2_area;
  gpointer window_data;
  double pos_x, pos_y, rot_x, rot_y, start_y, angle;
  GdkWindow *parent;

  parent = gdk_window_get_parent (child);
  gdk_window_get_user_data (parent, &window_data);
  offscreen_box = window_data;

  start_y = offscreen_box->child1 ? offscreen_box->child1->allocation.height : 0;
  child2_area = offscreen_box->child2->allocation;

  pos_x = parent_x - child2_area.width / 2;
  pos_y = parent_y - start_y - child2_area.height / 2;

  angle = -offscreen_box->angle;
  rot_x = pos_x * cos (angle) - pos_y * sin (angle);
  rot_y = pos_x * sin (angle) + pos_y * cos (angle);

  *child_x = rot_x + child2_area.width / 2;
  *child_y = rot_y + child2_area.height / 2;
}

static void
to_parent (GdkWindow *child,
	   gdouble    child_x,
	   gdouble    child_y,
	   gdouble   *parent_x,
	   gdouble   *parent_y)
{
  GtkOffscreenBox *offscreen_box = NULL;
  GtkAllocation child2_area;
  gpointer window_data;
  gdouble pos_x, pos_y, rot_x, rot_y, start_y, angle;
  GdkWindow *parent;
	   
  parent = gdk_window_get_parent (child);
  gdk_window_get_user_data (parent, &window_data);
  offscreen_box = window_data;

  start_y = offscreen_box->child1 ? offscreen_box->child1->allocation.height : 0;
  child2_area = offscreen_box->child2->allocation;

  pos_x = child_x - child2_area.width / 2;
  pos_y = child_y - child2_area.height / 2;

  angle = offscreen_box->angle;
  rot_x = pos_x * cos (angle) - pos_y * sin (angle);
  rot_y = pos_x * sin (angle) + pos_y * cos (angle);

  rot_x += child2_area.width / 2;
  rot_y += child2_area.height / 2;

  *parent_x = rot_x;
  *parent_y = rot_y + start_y;
}