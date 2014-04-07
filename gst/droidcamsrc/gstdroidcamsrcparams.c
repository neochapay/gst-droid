/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstdroidcamsrcparams.h"
#include <stdlib.h>
#include <gst/video/video.h>
#include "gst/memory/gstgralloc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

static void
gst_droidcamsrc_params_destroy_list (gpointer data)
{
  GList *list = (GList *) data;

  g_list_free_full (list, (GDestroyNotify) g_free);
}

static void
gst_droidcamsrc_params_parse_key_values (GstDroidCamSrcParams * params,
    const char *key, const char *values)
{
  char **value = g_strsplit (values, ",", -1);
  char **val = value;
  GList *list = NULL;

  while (*val) {
    list = g_list_append (list, g_strdup (*val));
    ++val;
  }

  g_hash_table_insert (params->params, g_strdup (key), list);

  g_strfreev (value);
}

static void
gst_droidcamsrc_params_parse_key_value (GstDroidCamSrcParams * params,
    const char *key, const char *value)
{
  if (g_strrstr (value, ",")) {
    /* needs farther splitting */
    gst_droidcamsrc_params_parse_key_values (params, key, value);
  } else {
    /* we are done */
    g_hash_table_insert (params->params, g_strdup (key), g_list_append (NULL,
            g_strdup (value)));
  }
}

static void
gst_droidcamsrc_params_parse (GstDroidCamSrcParams * params, const char *part)
{
  gchar **parts = g_strsplit (part, "=", 2);
  gchar *key = parts[0];
  gchar *value = key ? parts[1] : NULL;

  GST_LOG ("param %s = %s", key, value);

  if (!key || !value) {
    goto out;
  }

  gst_droidcamsrc_params_parse_key_value (params, key, value);

out:
  g_strfreev (parts);
}

static GList *
gst_droidcamsrc_params_get_item_locked (GstDroidCamSrcParams * params,
    const char *key)
{
  GList *list = g_hash_table_lookup (params->params, key);
  if (!list) {
    return NULL;
  }

  return list;
}

#if 0
static gchar *
gst_droidcamsrc_params_get_string_locked (GstDroidCamSrcParams * params,
    const char *key)
{
  GList *list = gst_droidcamsrc_params_get_item_locked (params, key);
  if (!list) {
    return NULL;
  }

  return list->data;
}
#endif
static int
gst_droidcamsrc_params_get_int_locked (GstDroidCamSrcParams * params,
    const char *key)
{
  GList *list = gst_droidcamsrc_params_get_item_locked (params, key);
  if (!list) {
    return -1;
  }

  return atoi (list->data);
}

static gboolean
gst_droidcamsrc_params_parse_dimension (char *d, int *w, int *h)
{
  char **sizes = g_strsplit (d, "x", -1);
  *w = sizes && sizes[0] ? atoi (sizes[0]) : -1;
  *h = *w != -1 && sizes[1] ? atoi (sizes[1]) : -1;

  g_strfreev (sizes);

  return *w != -1 && *h != -1;
}

GstDroidCamSrcParams *
gst_droidcamsrc_params_new (const gchar * params)
{
  GstDroidCamSrcParams *param = g_slice_new0 (GstDroidCamSrcParams);
  gchar **parts = g_strsplit (params, ";", -1);
  gchar **part = parts;

  GST_DEBUG ("params new");

  param->params = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free,
      (GDestroyNotify) gst_droidcamsrc_params_destroy_list);

  while (*part) {
    gst_droidcamsrc_params_parse (param, *part);
    ++part;
  }

  g_strfreev (parts);

  g_mutex_init (&param->lock);

  param->is_dirty = FALSE;

  return param;
}

void
gst_droidcamsrc_params_destroy (GstDroidCamSrcParams * params)
{
  GST_DEBUG ("params destroy");

  g_mutex_clear (&params->lock);
  g_hash_table_unref (params->params);
  g_slice_free (GstDroidCamSrcParams, params);
}

gchar *
gst_droidcamsrc_params_to_string (GstDroidCamSrcParams * params)
{
  gchar *string = NULL;
  GHashTableIter iter;
  gpointer key, value;

  g_mutex_lock (&params->lock);

  g_hash_table_iter_init (&iter, params->params);

  /* ugly is the least to be said */
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GList *list = (GList *) value;
    gchar *str = NULL;
    int len = g_list_length (list);
    g_assert (len > 0);

    if (len == 1) {
      /* simple case */
      str = g_strdup_printf ("%s=%s", (gchar *) key, (gchar *) list->data);
    } else {
      int x;
      for (x = 0; x < len; x++) {
        GList *item = g_list_nth (list, x);
        if (str == NULL) {
          str = g_strdup_printf ("%s=%s", (gchar *) key, (gchar *) item->data);
        } else {
          gchar *new_str = g_strjoin (",", str, (gchar *) item->data, NULL);
          g_free (str);
          str = new_str;
        }
      }
    }

    if (string == NULL) {
      string = str;
    } else {
      gchar *new_string = g_strjoin (";", string, str, NULL);
      g_free (string);
      g_free (str);
      string = new_string;
    }
  }

  g_mutex_unlock (&params->lock);

  return string;
}

gboolean
gst_droidcamsrc_params_is_dirty (GstDroidCamSrcParams * params)
{
  gboolean is_dirty;

  g_mutex_lock (&params->lock);
  is_dirty = params->is_dirty;
  g_mutex_unlock (&params->lock);

  return is_dirty;
}

GstCaps *
gst_droidcamsrc_params_get_viewfinder_caps (GstDroidCamSrcParams * params)
{
  int fps;
  GstVideoInfo info;
  GstCapsFeatures *feature;
  GstCaps *caps = gst_caps_new_empty ();
  GList *item;

  g_mutex_lock (&params->lock);

  fps = gst_droidcamsrc_params_get_int_locked (params, "preview-frame-rate");
  if (fps == -1) {
    goto unlock_and_out;
  }

  item = gst_droidcamsrc_params_get_item_locked (params, "preview-size-values");
  if (!item) {
    goto unlock_and_out;
  }

  while (item) {
    int width, height;
    GstCaps *caps2;

    if (gst_droidcamsrc_params_parse_dimension (item->data, &width, &height)) {
      gst_video_info_init (&info);
      gst_video_info_set_format (&info, GST_VIDEO_FORMAT_ENCODED, width,
          height);

      GST_VIDEO_INFO_FPS_N (&info) = fps;
      GST_VIDEO_INFO_FPS_D (&info) = 1;
      caps2 = gst_video_info_to_caps (&info);
      feature =
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DROID_SURFACE, NULL);
      gst_caps_set_features (caps2, 0, feature);

      caps = gst_caps_merge (caps, caps2);
    }

    item = g_list_next (item);
  }

unlock_and_out:
  g_mutex_unlock (&params->lock);

  return caps;
}

GstCaps *
gst_droidcamsrc_params_get_video_caps (GstDroidCamSrcParams * params)
{
  GstCaps *caps = NULL;

  g_mutex_lock (&params->lock);

  g_mutex_unlock (&params->lock);

  return caps;
}

GstCaps *
gst_droidcamsrc_params_get_image_caps (GstDroidCamSrcParams * params)
{
  GstCaps *caps = NULL;

  g_mutex_lock (&params->lock);

  g_mutex_unlock (&params->lock);

  return caps;
}