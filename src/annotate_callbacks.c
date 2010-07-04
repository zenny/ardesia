/* 
 * Ardesia -- a program for painting on the screen
 * with this program you can play, draw, learn and teach
 * This program has been written such as a freedom sonet
 * We believe in the freedom and in the freedom of education
 *
 * Copyright (C) 2009 Pilolli Pietro <pilolli@fbk.eu>
 *
 * Ardesia is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Ardesia is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * Functions for handling various (GTK+)-Events
 */

#include <annotate_callbacks.h>
#include <annotate.h>
#include <utils.h>


/* Expose event: this occurs when the windows is show */
G_MODULE_EXPORT gboolean
event_expose (GtkWidget *widget, 
              GdkEventExpose *event, 
              gpointer func_data)
{
  AnnotateData *data = (AnnotateData *) func_data;
  if (data->are_annotations_hidden)
    {
      return TRUE;
    }

  int is_fullscreen = gdk_window_get_state (widget->window) & GDK_WINDOW_STATE_FULLSCREEN;
  if (!is_fullscreen)
    {
      return TRUE;
    }

  if (data->debug)
    {
      g_printerr("Expose event\n");
    }

  if (!(data->annotation_cairo_context))
    {
      /* initialize a transparent window */	 
      #ifdef _WIN32
        HDC hdc = GetDC (GDK_WINDOW_HWND (data->annotation_window->window));
        /* This creare a surface with RGB24 colormap */
	    cairo_surface_t* surface = cairo_win32_surface_create(hdc);
	    /* 
         * @TODO Try to hack the cairo_win32_surface_create code to create a ARGB32 colormap to support the alpha channel
         *  in this way will fix the highlighter feature 
         */
	    //cairo_surface_t* surface = cairo_win32_surface_create_with_ddb(hdc, CAIRO_FORMAT_ARGB32, data->width, data->height);
	    data->annotation_cairo_context = cairo_create(surface); 
      #else
	    data->annotation_cairo_context = gdk_cairo_create(data->annotation_window->window);  
      #endif 
      if (cairo_status(data->annotation_cairo_context) != CAIRO_STATUS_SUCCESS)
        {
          g_printerr ("Unable to allocate the annotation cairo context"); 
          annotate_quit(); 
          exit(1);
        }     
		
      annotate_clear_screen();

      annotate_set_thickness(14);
      annotate_toggle_grab();		 
      data->transparent_pixmap = gdk_pixmap_new (data->annotation_window->window,
                                                 data->width,
					         data->height, 
                                                 -1);

      data->transparent_cr = gdk_cairo_create(data->transparent_pixmap);
      if (cairo_status(data->transparent_cr) != CAIRO_STATUS_SUCCESS)
        {
          g_printerr ("Unable to allocate the transparent cairo context"); 
          annotate_quit(); 
          exit(1);
        }

      clear_cairo_context(data->transparent_cr); 
    }
  annotate_restore_surface();
  return TRUE;
}


/*
 * Event-Handlers to perform the drawing
 */


/* This is called when the button is pushed */
G_MODULE_EXPORT gboolean
paint (GtkWidget *win,
       GdkEventButton *ev, 
       gpointer func_data)
{ 

  AnnotateData *data = (AnnotateData *) func_data;
 
  if (!ev)
    {
       g_printerr("Device '%s': Invalid event; I ungrab all\n",
		   ev->device->name);
       annotate_release_grab ();
       return TRUE;
    }

  int x,y;
  #ifdef _WIN32
    /* I do not know why but sometimes the event coord are wrong */
    /* get cursor position */
    gdk_display_get_pointer (data->display, NULL, &x, &y, NULL);
  #else
    x = ev->x;
    y = ev->y;	
  #endif
  if (inside_bar_window(x, y))
    /* point is in the ardesia bar */
    {
      /* the last point was outside the bar then ungrab */
      annotate_release_grab ();
      return TRUE;
    }   
	
  if (data->are_annotations_hidden)
    {
      return TRUE;
    }
  annotate_coord_list_free();
 
  unhide_cursor();
  
  /* only button1 allowed */
  if (!(ev->button==1))
    {
     if (data->debug)
       {    
         g_printerr("Device '%s': Invalid pressure event\n",
		     ev->device->name);
       }
      return TRUE;
    }  

  if (data->debug)
    {    
      g_printerr("Device '%s': Button %i Down at (x,y)=(%d : %d)\n",
		  ev->device->name, ev->button, x, y);
    }

  reset_cairo();
  cairo_move_to(data->annotation_cairo_context, x, y);
 
  annotate_coord_list_prepend (x, y, data->thickness);
  return TRUE;
}


/* This shots when the ponter is moving */
G_MODULE_EXPORT gboolean
paintto (GtkWidget *win, 
         GdkEventMotion *ev, 
         gpointer func_data)
{
  AnnotateData *data = (AnnotateData *) func_data;
  
  if (!ev)
    {
       g_printerr("Device '%s': Invalid event; I ungrab all\n",
		 ev->device->name);
       annotate_release_grab ();
       return TRUE;
    }
  
  int x,y;
  #ifdef _WIN32
    /* I do not know why but sometimes the event coord are wrong */
    /* get cursor position */
    gdk_display_get_pointer (data->display, NULL, &x, &y, NULL);
  #else
    x = ev->x;
    y = ev->y;	
  #endif
  if (inside_bar_window(x, y))
    /* point is in the ardesia bar */
    {
       if (data->debug)
         {    
           g_printerr("Device '%s': Move on the bar then ungrab\n",
	              ev->device->name);
         }
      /* the last point was outside the bar then ungrab */
      annotate_release_grab ();
      return TRUE;
    }
  
  if (data->are_annotations_hidden)
    {
      return TRUE;
    }
	
  unhide_cursor();

  gdouble pressure = 1;
  GdkModifierType state = (GdkModifierType) ev->state;  
  gint selected_width = 0;
  /* only button1 allowed */
  if (ev->device->source != GDK_SOURCE_MOUSE)
    {
      gint width = (CLAMP (pressure * pressure,0,1) *
		    (double) data->thickness);
      if (width!=data->thickness)
        {
          
          cairo_set_line_width(data->annotation_cairo_context, width);
        }
    }
  else if (!(state & GDK_BUTTON1_MASK))
    {
       /* the button is not pressed */
      return TRUE;
    }

  selected_width = data->thickness * pressure;
  
  annotate_draw_line (x, y, TRUE);
   
  annotate_coord_list_prepend (x, y, selected_width);

  return TRUE;
}


/* This shots when the button is realeased */
G_MODULE_EXPORT gboolean
paintend (GtkWidget *win, GdkEventButton *ev, gpointer func_data)
{
  AnnotateData *data = (AnnotateData *) func_data;
  
  if (!ev)
    {
       g_printerr("Device '%s': Invalid event; I ungrab all\n",
		 ev->device->name);
       annotate_release_grab ();
       return TRUE;
    }
  
  int x,y;
  #ifdef _WIN32
    /* I do not know why but sometimes the event coord are wrong */
    /* get cursor position */
    gdk_display_get_pointer (data->display, NULL, &x, &y, NULL);
  #else
    x = ev->x;
    y = ev->y;	
  #endif
  if (inside_bar_window(x, y))
    /* point is in the ardesia bar */
    {
      /* the last point was outside the bar then ungrab */
      annotate_release_grab ();
      return TRUE;
    }
  
  if (data->are_annotations_hidden)
    {
      return TRUE;
    }
	
  if (data->debug)
    {
      g_printerr("Device '%s': Button %i Up at (x,y)=(%.2f : %.2f)\n",
		 ev->device->name, ev->button, ev->x, ev->y);
    }

  /* only button1 allowed */
  if (!(ev->button==1))
    {
      return TRUE;
    }  

  int distance = -1;
  if (!data->coordlist)
    {
      annotate_draw_point(x, y);  
      annotate_coord_list_prepend (x, y, data->thickness);
    }
  else
    { 
      gint lenght = g_slist_length(data->coordlist);
      AnnotateStrokeCoordinate* first_point = (AnnotateStrokeCoordinate*) g_slist_nth_data (data->coordlist, lenght-1);
       
      /* This is the tollerance to force to close the path in a magnetic way */
      int tollerance = data->thickness;
      distance = get_distance(x, y, first_point->x, first_point->y);
 
      /* If the distance between two point lesser than tollerance they are the same point for me */
      if (distance<=tollerance)
        {
          distance=0;
	  cairo_line_to(data->annotation_cairo_context, first_point->x, first_point->y);
          annotate_coord_list_prepend (first_point->x, first_point->y, data->thickness);
	}
      else
        {
          cairo_line_to(data->annotation_cairo_context, x, y);
          annotate_coord_list_prepend (x, y, data->thickness);
        }
   
      if (!(data->cur_context->type == ANNOTATE_ERASER))
        { 
          gboolean closed_path = (distance == 0) ; 
          shape_recognize(closed_path);
          /* If is selected an arrowtype the draw the arrow */
          if (data->arrow)
            {
	      /* print arrow at the end of the line */
	      annotate_draw_arrow (distance);
	    }
         }
    }
  cairo_stroke_preserve(data->annotation_cairo_context);
  add_save_point();
  hide_cursor();  
 
  return TRUE;
}


/* Device touch */
G_MODULE_EXPORT gboolean
proximity_in (GtkWidget *win,
              GdkEventProximity *ev, 
              gpointer func_data)
{
  AnnotateData *data = (AnnotateData *) func_data;
  if (data->are_annotations_hidden)
    {
      return TRUE;
    }
  gint x, y;
  GdkModifierType state;

  gdk_window_get_pointer (data->annotation_window->window, &x, &y, &state);
  annotate_select_tool (ev->device, state);
  if (data->debug)
    {
      g_printerr("Proximity in device %s\n", ev->device->name);
    }
  return TRUE;
}


/* Device lease */
G_MODULE_EXPORT gboolean
proximity_out (GtkWidget *win, 
               GdkEventProximity *ev,
               gpointer func_data)
{
  AnnotateData *data = (AnnotateData *) func_data;
  if (data->are_annotations_hidden)
    {
      return TRUE;
    }
  data->cur_context = data->default_pen;

  data->device = NULL;
  if (data->debug)
    {
      g_printerr("Proximity out device %s\n", ev->device->name);
    }
  return FALSE;
}

