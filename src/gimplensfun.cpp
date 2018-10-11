/*
 *
 *
 * Copyright 2010-2011 Sebastian Kraft
 *
 * This file is part of GimpLensfun.
 *
 * GimpLensfun is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version
 * 3 of the License, or (at your option) any later version.
 *
 * GimpLensfun is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with GimpLensfun. If not, see
 * http://www.gnu.org/licenses/.
 *
 */

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include <lensfun/lensfun.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <exiv2/exif.hpp>
#include <gexiv2/gexiv2.h>
#include "LUT.hpp"

#define PLUG_IN_PROC   "plug-in-lensfun"
#define PLUG_IN_BINARY "lensfun"
#define PLUG_IN_ROLE   "gimp-lensfun"

#define VERSION "0.2.5-dev"

#ifndef DEBUG
#define DEBUG false
#endif

using namespace std;

// ############################################################################
// Function declarations

static void
query ();

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals);

// ############################################################################
// Global variables

GtkWidget  *camera_combo, *maker_combo, *lens_combo;
GtkWidget  *corr_vignetting, *corr_TCA, *corr_distortion;
lfDatabase *ldb;
bool       combo_box_lock = false;

// ############################################################################
// Interpolation parameters
const gint  lanczos_width     = 2;
const gint  lanczos_table_res = 256;
LUT<gfloat> lanczos_LUT (lanczos_width * 2 * lanczos_table_res + 1);

typedef enum GL_INTERPOL
{
  GL_INTERPOL_NN, // Nearest Neighbour
  GL_INTERPOL_BL, // Bilinear
  GL_INTERPOL_LZ  // Lanczos
} GlInterpolationType;

// ############################################################################
// List of camera makers

const string camera_makers[] = {
  "Canon",
  "Casio",
  "Fujifilm",
  "GoPro",
  "Kodak",
  "Konica",
  "Leica",
  "Nikon",
  "Olympus",
  "Panasonic",
  "Pentax",
  "Ricoh",
  "Samsung",
  "Sigma",
  "Sony",
  "nullptr"
};

// ############################################################################
// Struct for holding camera/lens info and parameters

typedef struct
{
  gint       modify_flags;
  bool       inverse;
  string     camera;
  string     cam_maker;
  string     lens;
  gfloat     scale;
  gfloat     crop;
  gfloat     focal;
  gfloat     aperture;
  gfloat     distance;
  lfLensType target_geom;
} MyLensfunOpts;

static MyLensfunOpts lensfun_params = {
  LF_MODIFY_DISTORTION,
  false,
  "",
  "",
  "",
  0.0,
  0,
  0,
  0,
  1.0,
  LF_RECTILINEAR
};

// ############################################################################
// Struct for storing camera/lens info and parameters

typedef struct
{
  gint       modify_flags;
  bool       inverse;
  gchar      camera[255];
  gchar      cam_maker[255];
  gchar      lens[255];
  gfloat     scale;
  gfloat     crop;
  gfloat     focal;
  gfloat     aperture;
  gfloat     distance;
  lfLensType target_geom;
} MyLensfunOptStorage;

static MyLensfunOptStorage lensfun_param_storage = {
  LF_MODIFY_DISTORTION,
  false,
  "",
  "",
  "",
  0.0,
  0,
  0,
  0,
  1.0,
  LF_RECTILINEAR
};

GimpPlugInInfo PLUG_IN_INFO = {
  nullptr,
  nullptr,
  query,
  run
};

MAIN()

// ############################################################################
// query() function

static void
query ()
{
  static GimpParamDef args[] = {
    {
      GIMP_PDB_INT32,
      (gchar *) "run-mode",
      (gchar *) "Run mode"
    },
    {
      GIMP_PDB_IMAGE,
      (gchar *) "image",
      (gchar *) "Input image"
    },
    {
      GIMP_PDB_DRAWABLE,
      (gchar *) "drawable",
      (gchar *) "Input drawable"
    }
  };

  gimp_install_procedure (
    PLUG_IN_PROC,
    "Correct lens distortion with lensfun",
    "Correct lens distortion with lensfun",
    "Sebastian Kraft",
    "Copyright Sebastian Kraft",
    "2010",
    "_GimpLensfun...",
    "RGB",
    GIMP_PLUGIN,
    G_N_ELEMENTS (args),
    0,
    args,
    nullptr);

  gimp_plugin_menu_register (PLUG_IN_PROC, "<Image>/Filters/Enhance");
}

// ############################################################################
// Some helper functions

inline gint
round_float_to_gint (gfloat d)
{
  return (gint) (d < 0 ? d - .5 : d + .5);
}

inline guchar
round_float_to_guchar (gfloat d)
{
  return (guchar) (d >= 0 ? MIN (d + .5, 255.0f) : 0.0f);
}

void
str_replace (string &str, const string &old, const string &new_str)
{
  size_t pos = 0;
  while ((pos = str.find (old, pos)) != string::npos)
  {
    str.replace (pos, old.length (), new_str);
    pos += new_str.length ();
  }
}

gint
str_compare (const string &str1,
             const string &str2,
             bool case_sensitive = false)
{
  string s1 = str1;
  string s2 = str2;

  if (!case_sensitive)
  {
    transform (s1.begin (), s1.end (), s1.begin (), ::tolower);
    transform (s2.begin (), s2.end (), s2.begin (), ::tolower);
  }

  return s1.compare (s2);

}

#ifdef POSIX
gulong
time_spec_to_gulong (struct timespec *ts)
{
  return (gulong) (((gulong)ts->tv_sec * 1000000000) + ts->tv_nsec);
}
#endif

// ############################################################################
// Helper functions for printing debug output

#if DEBUG
static void
print_mount (const lfMount *mount)
{
  g_print ("Mount: %s\n", lf_mlstr_get (mount->Name));
  if (mount->Compat)
    for (gint j = 0; mount->Compat [j]; j++)
      g_print ("\tCompat: %s\n", mount->Compat [j]);
}

static void
print_camera (const lfCamera *camera)
{
  g_print ("Camera: %s / %s %s%s%s\n",
           lf_mlstr_get (camera->Maker),
           lf_mlstr_get (camera->Model),
           camera->Variant ? "(" : "",
           camera->Variant ? lf_mlstr_get (camera->Variant) : "",
           camera->Variant ? ")" : "");
  g_print ("\tMount: %s\n", lf_db_mount_name (ldb, camera->Mount));
  g_print ("\tCrop factor: %g\n", camera->CropFactor);
}

static void
print_lens (const lfLens *lens)
{
  g_print ("Lens: %s / %s\n",
           lf_mlstr_get (lens->Maker),
           lf_mlstr_get (lens->Model));
  g_print ("\tCrop factor: %g\n", lens->CropFactor);
  g_print ("\tFocal: %g-%g\n", lens->MinFocal, lens->MaxFocal);
  g_print ("\tAperture: %g-%g\n", lens->MinAperture, lens->MaxAperture);
  g_print ("\tCenter: %g,%g\n", lens->CenterX, lens->CenterY);
  if (lens->Mounts)
    for (gint j = 0; lens->Mounts [j]; j++)
      g_print ("\tMount: %s\n", lf_db_mount_name (ldb, lens->Mounts [j]));
}

static void
print_cameras (const lfCamera **cameras)
{
  if (cameras)
  {
    for (gint i = 0; cameras [i]; i++)
    {
      g_print ("--- camera %d: ---\n", i + 1);
      print_camera (cameras[i]);
    }
  }
  else
  {
    g_print ("\t- failed\n");
  }
}

static void
print_lenses (const lfLens **lenses)
{
  if (lenses)
  {
    for (gint i = 0; lenses [i]; i++)
    {
      g_print ("--- lens %d, score %d: ---\n", i + 1, lenses [i]->Score);
      print_lens (lenses[i]);
    }
  }
  else
  {
    g_print ("\t- failed\n");
  }
}
#endif

// ############################################################################
// Set dialog combo boxes to values

static void
dialog_set_cboxes (const string new_make,
                   const string new_camera,
                   const string new_lens)
{
  vector<string> camera_list;
  vector<string> lens_list;

  const lfCamera **cameras = nullptr;
  const lfLens   **lenses  = nullptr;
  GtkTreeModel   *store    = nullptr;

  gint curr_maker_ID  = -1;
  gint curr_camera_ID = -1;
  gint curr_lens_ID   = -1;

  lensfun_params.cam_maker.clear ();
  lensfun_params.camera.clear ();
  lensfun_params.lens.clear ();

  if (new_make.empty ())
    return;

  // Try to match maker with predefined list
  gint num_makers = 0;
  for (gint i  = 0; camera_makers[i] != "nullptr"; i++)
  {
    if (str_compare (camera_makers[i], new_make) == 0)
    {
      gtk_combo_box_set_active (GTK_COMBO_BOX(maker_combo), i);
      curr_maker_ID = i;
    }
    num_makers++;
  }

  if (curr_maker_ID >= 0)
  {
    lensfun_params.cam_maker = camera_makers[curr_maker_ID];
  }
  else
  {
    gtk_combo_box_append_text (GTK_COMBO_BOX(maker_combo), new_make.c_str ());
    gtk_combo_box_set_active (GTK_COMBO_BOX(maker_combo), num_makers);
    num_makers++;
    lensfun_params.cam_maker = new_make;
  }

  // Clear camera / lens combobox
  store = gtk_combo_box_get_model (GTK_COMBO_BOX(camera_combo));
  gtk_list_store_clear (GTK_LIST_STORE(store));
  store = gtk_combo_box_get_model (GTK_COMBO_BOX(lens_combo));
  gtk_list_store_clear (GTK_LIST_STORE(store));

  // Get all cameras from maker out of database
  cameras = ldb->FindCamerasExt (lensfun_params.cam_maker.c_str (),
                                 nullptr,
                                 LF_SEARCH_LOOSE);
  if (cameras)
  {
    for (gint i = 0; cameras[i]; i++)
    {
      camera_list.push_back (string (lf_mlstr_get (cameras[i]->Model)));
    }
    sort (camera_list.begin (), camera_list.end ());
  }
  else
  {
    return;
  }

  for (guint i = 0; i < camera_list.size (); i++)
  {
    gtk_combo_box_append_text (GTK_COMBO_BOX(camera_combo),
                               camera_list[i].c_str ());
    // Set to active if model matches current camera
    if ((!new_camera.empty ()) &&
        (str_compare (new_camera, camera_list[i]) == 0))
    {
      gtk_combo_box_set_active (GTK_COMBO_BOX(camera_combo), i);
      lensfun_params.camera = new_camera;
    }

    if (str_compare (string (lf_mlstr_get (cameras[i]->Model)), new_camera) ==
        0)
      curr_camera_ID = i;
  }

  // Return if camera is unidentified
  if (curr_camera_ID == -1)
  {
    lf_free (cameras);
    return;
  }

  // Find lenses for camera model
  lenses = ldb->FindLenses (cameras[curr_camera_ID], nullptr, nullptr);
  if (lenses)
  {
    lens_list.clear ();
    for (gint i = 0; lenses[i]; i++)
      lens_list.push_back (string (lf_mlstr_get (lenses[i]->Model)));
    sort (lens_list.begin (), lens_list.end ());
  }
  else
  {
    lf_free (cameras);
    return;
  }

  for (guint i = 0; i < lens_list.size (); i++)
  {
    gtk_combo_box_append_text (GTK_COMBO_BOX(lens_combo),
                               (lens_list[i]).c_str ());

    // Set active if lens matches current lens model
    if ((!new_lens.empty ()) && (str_compare (new_lens, lens_list[i]) == 0))
    {
      gtk_combo_box_set_active (GTK_COMBO_BOX(lens_combo), i);
      lensfun_params.lens = new_lens;
    }

    if (str_compare (string (lf_mlstr_get (lenses[i]->Model)), new_lens) == 0)
      curr_lens_ID = i;
  }

  gtk_widget_set_sensitive (corr_TCA, false);
  gtk_widget_set_sensitive (corr_vignetting, false);

  if (curr_lens_ID >= 0)
  {
    if (lenses[curr_lens_ID]->CalibTCA != nullptr)
      gtk_widget_set_sensitive (corr_TCA, true);
    if (lenses[curr_lens_ID]->CalibVignetting != nullptr)
      gtk_widget_set_sensitive (corr_vignetting, true);
  }

  lf_free (lenses);
  lf_free (cameras);
}

// ############################################################################
// Dialog callback functions

static void
maker_cb_changed (GtkComboBox *combo, gpointer data)
{
  if (!combo_box_lock)
  {
    combo_box_lock = true;
    dialog_set_cboxes (
      gtk_combo_box_get_active_text (GTK_COMBO_BOX (maker_combo)),
      "",
      ""
                      );
    combo_box_lock = false;
  }
}

static void
camera_cb_changed (GtkComboBox *combo, gpointer data)
{
  if (!combo_box_lock)
  {
    combo_box_lock = true;
    dialog_set_cboxes (
      string (gtk_combo_box_get_active_text (GTK_COMBO_BOX (maker_combo))),
      string (gtk_combo_box_get_active_text (GTK_COMBO_BOX (camera_combo))),
      ""
                      );
    combo_box_lock = false;
  }
}

static void
lens_cb_changed (GtkComboBox *combo, gpointer data)
{
  if (!combo_box_lock)
  {
    combo_box_lock = true;
    dialog_set_cboxes (
      string (gtk_combo_box_get_active_text (GTK_COMBO_BOX (maker_combo))),
      string (gtk_combo_box_get_active_text (GTK_COMBO_BOX (camera_combo))),
      string (gtk_combo_box_get_active_text (GTK_COMBO_BOX (lens_combo)))
                      );
    combo_box_lock = false;
  }
}

static void
focal_changed (GtkComboBox *combo, gpointer data)
{
  lensfun_params.focal =
    (gfloat) gtk_adjustment_get_value (GTK_ADJUSTMENT (data));
}

static void
aperture_changed (GtkComboBox *combo, gpointer data)
{
  lensfun_params.aperture =
    (gfloat) gtk_adjustment_get_value (GTK_ADJUSTMENT (data));
}

static void
scale_check_changed (GtkCheckButton *togglebutn, gpointer data)
{
  lensfun_params.scale =
    !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (togglebutn));
}

static void
modify_changed (GtkCheckButton *togglebutn, gpointer data)
{
  lensfun_params.modify_flags = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (corr_distortion)) &&
      GTK_WIDGET_SENSITIVE (corr_distortion))
    lensfun_params.modify_flags |= LF_MODIFY_DISTORTION;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (corr_TCA)) &&
      GTK_WIDGET_SENSITIVE (corr_TCA))
    lensfun_params.modify_flags |= LF_MODIFY_TCA;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (corr_vignetting)) &&
      GTK_WIDGET_SENSITIVE (corr_vignetting))
    lensfun_params.modify_flags |= LF_MODIFY_VIGNETTING;
}

// ############################################################################
// Create gtk dialog window

static bool
create_dialog_window ()
{
  GtkWidget *dialog;
  GtkWidget *main_vbox;
  GtkWidget *frame, *frame2;
  GtkWidget *camera_label, *lens_label, *maker_label;
  GtkWidget *focal_label, *aperture_label;
  GtkWidget *scale_check;

  GtkWidget *spinbutton;
  GtkObject *spinbutton_adj;
  GtkWidget *spinbutton_aperture;
  GtkObject *spinbutton_aperture_adj;
  GtkWidget *frame_label, *frame_label2;
  GtkWidget *table, *table2;
  bool      run;

  guint table_row = 0;

  gimp_ui_init (PLUG_IN_BINARY, false);

  dialog = gimp_dialog_new (PLUG_IN_ROLE "(v" VERSION ")",
                            PLUG_IN_ROLE,
                            nullptr,
                            GTK_DIALOG_MODAL,
                            gimp_standard_help_func,
                            PLUG_IN_PROC,
                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK, GTK_RESPONSE_OK,
                            nullptr);

  main_vbox = gtk_vbox_new (false, 6);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), main_vbox);
  gtk_widget_show (main_vbox);

  frame = gtk_frame_new (nullptr);
  gtk_widget_show (frame);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, true, true, 0);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);

  frame_label = gtk_label_new ("camera/lens Parameters");
  gtk_widget_show (frame_label);
  gtk_frame_set_label_widget (GTK_FRAME (frame), frame_label);
  gtk_label_set_use_markup (GTK_LABEL (frame_label), true);

  table = gtk_table_new (6, 2, true);
  gtk_table_set_homogeneous (GTK_TABLE (table), false);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 2);
  gtk_container_set_border_width (GTK_CONTAINER (table), 10);

  // Camera maker
  maker_label = gtk_label_new ("Maker:");
  gtk_misc_set_alignment (GTK_MISC (maker_label), 0.0, 0.5);
  gtk_widget_show (maker_label);
  gtk_table_attach (GTK_TABLE (table),
                    maker_label,
                    0,
                    1,
                    table_row,
                    table_row + 1,
                    GTK_FILL,
                    GTK_FILL,
                    0,
                    0);

  maker_combo = gtk_combo_box_new_text ();
  gtk_widget_show (maker_combo);

  for (gint i = 0; str_compare (camera_makers[i], "nullptr") != 0; i++)
    gtk_combo_box_append_text (GTK_COMBO_BOX (maker_combo),
                               camera_makers[i].c_str ());

  gtk_table_attach_defaults (GTK_TABLE (table),
                             maker_combo,
                             1,
                             2,
                             table_row,
                             table_row + 1);

  table_row++;

  // Camera
  camera_label = gtk_label_new ("camera:");
  gtk_misc_set_alignment (GTK_MISC (camera_label), 0.0, 0.5);
  gtk_widget_show (camera_label);
  gtk_table_attach (GTK_TABLE (table),
                    camera_label,
                    0,
                    1,
                    table_row,
                    table_row + 1,
                    GTK_FILL,
                    GTK_FILL,
                    0,
                    0);

  camera_combo = gtk_combo_box_new_text ();
  gtk_widget_show (camera_combo);

  gtk_table_attach_defaults (GTK_TABLE (table),
                             camera_combo,
                             1,
                             2,
                             table_row,
                             table_row + 1);

  table_row++;

  // Lens
  lens_label = gtk_label_new ("lens:");
  gtk_misc_set_alignment (GTK_MISC (lens_label), 0.0, 0.5);
  gtk_widget_show (lens_label);
  gtk_table_attach_defaults (GTK_TABLE (table),
                             lens_label,
                             0,
                             1,
                             table_row,
                             table_row + 1);

  lens_combo = gtk_combo_box_new_text ();
  gtk_widget_show (lens_combo);

  gtk_table_attach_defaults (GTK_TABLE (table),
                             lens_combo,
                             1,
                             2,
                             table_row,
                             table_row + 1);
  table_row++;

  // Focal length
  focal_label = gtk_label_new ("focal length (mm):");
  gtk_misc_set_alignment (GTK_MISC (focal_label), 0.0, 0.5);
  gtk_widget_show (focal_label);
  gtk_table_attach_defaults (GTK_TABLE (table),
                             focal_label,
                             0,
                             1,
                             table_row,
                             table_row + 1);

  spinbutton_adj = gtk_adjustment_new (lensfun_params.focal,
                                       0,
                                       5000,
                                       0.1,
                                       0,
                                       0);
  spinbutton     = gtk_spin_button_new (GTK_ADJUSTMENT (spinbutton_adj), 2, 1);
  gtk_widget_show (spinbutton);
  gtk_table_attach_defaults (GTK_TABLE (table),
                             spinbutton,
                             1,
                             2,
                             table_row,
                             table_row + 1);
  table_row++;

  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinbutton), true);

  // Aperture
  aperture_label = gtk_label_new ("Aperture:");
  gtk_misc_set_alignment (GTK_MISC (aperture_label), 0.0, 0.5);
  gtk_widget_show (focal_label);
  gtk_table_attach_defaults (GTK_TABLE (table),
                             aperture_label,
                             0,
                             1,
                             table_row,
                             table_row + 1);

  spinbutton_aperture_adj = gtk_adjustment_new (lensfun_params.aperture,
                                                0,
                                                128,
                                                0.1,
                                                0,
                                                0);
  spinbutton_aperture     = gtk_spin_button_new (GTK_ADJUSTMENT (
                                                   spinbutton_aperture_adj
                                                                ),
                                                 2,
                                                 1);
  gtk_widget_show (spinbutton_aperture);
  gtk_table_attach_defaults (GTK_TABLE (table),
                             spinbutton_aperture,
                             1,
                             2,
                             table_row,
                             table_row + 1);
  table_row++;

  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinbutton_aperture), true);

  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_widget_show_all (table);

  frame2 = gtk_frame_new (nullptr);
  gtk_widget_show (frame2);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame2, true, true, 0);
  gtk_container_set_border_width (GTK_CONTAINER (frame2), 6);

  frame_label2 = gtk_label_new ("Processing Parameters");
  gtk_widget_show (frame_label2);
  gtk_frame_set_label_widget (GTK_FRAME (frame2), frame_label2);
  gtk_label_set_use_markup (GTK_LABEL (frame_label2), true);

  table2 = gtk_table_new (6, 2, true);
  gtk_table_set_homogeneous (GTK_TABLE (table2), false);
  gtk_table_set_row_spacings (GTK_TABLE (table2), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table2), 2);
  gtk_container_set_border_width (GTK_CONTAINER (table2), 10);

  table_row = 0;

  // Scale to fit checkbox
  scale_check = gtk_check_button_new_with_label ("scale to fit");
  //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), false);
  gtk_widget_show (scale_check);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scale_check),
                                !lensfun_params.scale);
  gtk_table_attach_defaults (GTK_TABLE (table2),
                             scale_check,
                             1,
                             2,
                             table_row,
                             table_row + 1);
  table_row++;

  // Enable distortion correction
  corr_distortion = gtk_check_button_new_with_label ("Distortion");
  //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), false);
  gtk_widget_show (corr_distortion);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (corr_distortion), true);
  gtk_table_attach_defaults (GTK_TABLE (table2),
                             corr_distortion,
                             1,
                             2,
                             table_row,
                             table_row + 1);
  table_row++;

  // Enable vignetting correction
  corr_vignetting = gtk_check_button_new_with_label ("Vignetting");
  //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), false);
  gtk_widget_show (corr_vignetting);
  gtk_widget_set_sensitive (corr_vignetting, false);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (corr_vignetting), false);
  gtk_table_attach_defaults (GTK_TABLE (table2),
                             corr_vignetting,
                             1,
                             2,
                             table_row,
                             table_row + 1);
  table_row++;

  // Enable TCA correction
  corr_TCA = gtk_check_button_new_with_label ("Chromatic Aberration");
  //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), false);
  gtk_widget_show (corr_TCA);
  gtk_widget_set_sensitive (corr_TCA, false);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (corr_TCA), false);
  gtk_table_attach_defaults (GTK_TABLE (table2),
                             corr_TCA,
                             1,
                             2,
                             table_row,
                             table_row + 1);
  table_row++;

  gtk_container_add (GTK_CONTAINER (frame2), table2);
  gtk_widget_show_all (table2);

  // Try to set combo boxes to exif
  dialog_set_cboxes (lensfun_params.cam_maker,
                     lensfun_params.camera,
                     lensfun_params.lens);

  // Connect signals
  g_signal_connect (G_OBJECT (maker_combo), "changed",
                    G_CALLBACK (maker_cb_changed), nullptr);
  g_signal_connect (G_OBJECT (camera_combo), "changed",
                   G_CALLBACK (camera_cb_changed), nullptr);
  g_signal_connect (G_OBJECT (lens_combo), "changed",
                    G_CALLBACK (lens_cb_changed), nullptr);
  g_signal_connect (spinbutton_adj, "value_changed",
                    G_CALLBACK (focal_changed), spinbutton_adj);
  g_signal_connect (spinbutton_aperture_adj, "value_changed",
                    G_CALLBACK (aperture_changed), spinbutton_aperture_adj);
  g_signal_connect (G_OBJECT (scale_check), "toggled",
                    G_CALLBACK (scale_check_changed), nullptr);
  g_signal_connect (G_OBJECT (corr_distortion), "toggled",
                    G_CALLBACK (modify_changed), nullptr);
  g_signal_connect (G_OBJECT (corr_TCA), "toggled",
                    G_CALLBACK (modify_changed), nullptr);
  g_signal_connect (G_OBJECT (corr_vignetting), "toggled",
                    G_CALLBACK (modify_changed), nullptr);

  // Show and run
  gtk_widget_show (dialog);
  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);
  return run;
}

// ############################################################################
// Interpolation functions

inline gfloat
lanczos (gfloat x)
{
  if ((x < FLT_MIN) && (x > -FLT_MIN))
    return 1.0f;

  if ((x >= lanczos_width) || (x <= (-1) * lanczos_width))
    return 0.0f;

  gfloat xpi = x * static_cast<gfloat>(M_PI);
  return (lanczos_width * sin (xpi) * sin (xpi / lanczos_width)) / (xpi * xpi);
}

void
init_interpolation (GlInterpolationType int_type)
{
  switch (int_type)
  {
    case GL_INTERPOL_NN:
      break;
    case GL_INTERPOL_BL:
      break;
    case GL_INTERPOL_LZ:
      for (gint i = -lanczos_width * lanczos_table_res;
           i < lanczos_width * lanczos_table_res; i++)
      {
        lanczos_LUT[i + lanczos_width * lanczos_table_res] =
          lanczos (static_cast<gfloat>(i) /
                   static_cast<gfloat>(lanczos_table_res));
      }
      break;
  }
}

inline guchar
interpolate_lanczos (const guchar *buf,
                     gint          buf_w,
                     gint          buf_h,
                     gint          buf_bpp,
                     gfloat        xpos,
                     gfloat        ypos,
                     gint          chan)
{
  auto   xl   = gint (xpos);
  auto   yl   = gint (ypos);
  gfloat y    = 0.0f;
  gfloat norm = 0.0f;
  gfloat L    = 0.0f;

  // Border checking
  if ((xl - lanczos_width + 1 < 0) ||
      (xl + lanczos_width >= buf_w) ||
      (yl - lanczos_width + 1 < 0) ||
      (yl + lanczos_width >= buf_h))
  {
    return 0;
  }

  // Convolve with lanczos kernel
  for (gint i = xl - lanczos_width + 1; i < xl + lanczos_width; i++)
  {
    for (gint j = yl - lanczos_width + 1; j < yl + lanczos_width; j++)
    {
      L = lanczos_LUT[(xpos - static_cast<gfloat>(i)) *
                      static_cast<gfloat>(lanczos_table_res) +
                      static_cast<gfloat>(lanczos_width * lanczos_table_res)]
          * lanczos_LUT[(ypos - static_cast<gfloat>(j)) *
                        static_cast<gfloat>(lanczos_table_res) +
                        static_cast<gfloat>(lanczos_width * lanczos_table_res)];
      // L = lanczos(xpos - static_cast<gfloat>(i))
      //                   * lanczos(ypos - static_cast<gfloat>(j));
      y += static_cast<gfloat>
           (buf[(buf_bpp * buf_w * j) + (i * buf_bpp) + chan]) * L;
      norm += L;
    }
  }
  // Normalize
  y = y / norm;

  // Round to guchar and return
  return round_float_to_guchar (y);
}

inline gint
interpolate_linear (const guchar *buf,
                    gint         buf_w,
                    gint         buf_h,
                    gint         buf_bpp,
                    gfloat       xpos,
                    gfloat       ypos,
                    gint         chan)
{
  // Interpolated values in x and y  direction
  gfloat x1, x2, y;

  // Surrounding integer rounded coordinates
  gint xl, xr, yu, yl;

  xl = (gint) floor (xpos);
  xr = (gint) ceil (xpos + 1e-10);
  yu = (gint) floor (ypos);
  yl = (gint) ceil (ypos + 1e-10);

  // Border checking
  if ((xl < 0) || (xr >= buf_w) || (yu < 0) || (yl >= buf_h))
    return 0;

  auto px1y1 = (gfloat) buf[(buf_bpp * buf_w * yu) + (xl * buf_bpp) + chan];
  auto px1y2 = (gfloat) buf[(buf_bpp * buf_w * yl) + (xl * buf_bpp) + chan];
  auto px2y1 = (gfloat) buf[(buf_bpp * buf_w * yu) + (xr * buf_bpp) + chan];
  auto px2y2 = (gfloat) buf[(buf_bpp * buf_w * yl) + (xr * buf_bpp) + chan];

  x1 = (static_cast<gfloat>(xr) - xpos) * px1y1 +
       (xpos - static_cast<gfloat>(xl)) * px2y1;
  x2 = (static_cast<gfloat>(xr) - xpos) * px1y2 +
       (xpos - static_cast<gfloat>(xl)) * px2y2;

  y = (ypos - static_cast<gfloat>(yu)) * x2 +
      (static_cast<gfloat>(yl) - ypos) * x1;

  return round_float_to_gint (y);
}

inline gint
interpolate_nearest (guchar *buf,
                     gint   buf_w,
                     gint   buf_h,
                     gint   buf_bpp,
                     gfloat xpos,
                     gfloat ypos,
                     gint   chan)
{
  gint x = round_float_to_gint (xpos);
  gint y = round_float_to_gint (ypos);


  // Border checking
  if ((x < 0) || (x >= buf_w) || (y < 0) || (y >= buf_h))
    return 0;

  return buf[(buf_bpp * buf_w * y) + (x * buf_bpp) + chan];
}

// ############################################################################
// Processing

static void
process_image (gint32 drawable_id)
{
  GeglBuffer *r_buffer, *w_buffer;
  const Babl *drawable_fmt;
  gint       drawable_bpp;

  gint drawable_w, drawable_h;

  guchar *img_buf;
  guchar *img_buf_out;

  r_buffer = gimp_drawable_get_buffer (drawable_id);
  w_buffer = gimp_drawable_get_shadow_buffer (drawable_id);

  drawable_w = gegl_buffer_get_width (r_buffer);
  drawable_h = gegl_buffer_get_height (r_buffer);

  drawable_fmt = babl_format ("R'G'B' u8");

  drawable_bpp = babl_format_get_bytes_per_pixel (drawable_fmt);

  if ((lensfun_params.cam_maker.length () == 0) ||
      (lensfun_params.camera.length () == 0) ||
      (lensfun_params.lens.length () == 0))
  {
    return;
  }

#ifdef POSIX
  struct timespec profiling_start, profiling_stop;
#endif

  // Init input and output buffer
  img_buf     = g_new (guchar, drawable_bpp * drawable_w * drawable_h);
  img_buf_out = g_new (guchar, drawable_bpp * drawable_w * drawable_h);
  init_interpolation (GL_INTERPOL_LZ);

  // Copy pixel data from GIMP to internal buffer
  gegl_buffer_get (r_buffer,
                   GEGL_RECTANGLE (0, 0, drawable_w, drawable_h),
                   1.0,
                   drawable_fmt,
                   img_buf,
                   GEGL_AUTO_ROWSTRIDE,
                   GEGL_ABYSS_NONE);

  if (lensfun_params.scale < 1)
    lensfun_params.modify_flags |= LF_MODIFY_SCALE;

  const lfCamera **cameras =
                   ldb->FindCamerasExt (lensfun_params.cam_maker.c_str (),
                                        lensfun_params.camera.c_str ());

  lensfun_params.crop = cameras[0]->CropFactor;

  const lfLens **lenses = ldb->FindLenses (cameras[0],
                                           nullptr,
                                           lensfun_params.lens.c_str ());

  if (DEBUG)
  {
    g_print ("\nApplied settings:\n");
    g_print ("\tcamera: %s, %s\n", cameras[0]->Maker, cameras[0]->Model);
    g_print ("\tlens: %s\n", lenses[0]->Model);
    g_print ("\tfocal Length: %f\n", lensfun_params.focal);
    g_print ("\tF-Stop: %f\n", lensfun_params.aperture);
    g_print ("\tCrop Factor: %f\n", lensfun_params.crop);
    g_print ("\tscale: %f\n", lensfun_params.scale);
#ifdef POSIX
    clock_gettime(CLOCK_REALTIME, &profiling_start);
#endif
  }

  // Init lensfun modifier
  auto *mod = new lfModifier (lenses[0],
                              lensfun_params.crop,
                              drawable_w,
                              drawable_h);

  mod->Initialize (lenses[0],
                   LF_PF_U8,
                   lensfun_params.focal,
                   lensfun_params.aperture,
                   lensfun_params.distance,
                   lensfun_params.scale,
                   lensfun_params.target_geom,
                   lensfun_params.modify_flags,
                   lensfun_params.inverse);

  gint row_count = 0;
#pragma omp parallel
  {
    // Buffer containing undistorted coordinates for one row
    gfloat *undist_coord = g_new (gfloat, drawable_w * 2 * drawable_bpp);

    // Main loop for processing, iterate through rows
#pragma omp for
    for (gint i = 0; i < drawable_h; i++)
    {
      mod->ApplyColorModification (&img_buf[(drawable_bpp * drawable_w * i)],
                                   0, i, drawable_w, 1,
                                   LF_CR_3(RED, GREEN, BLUE),
                                   drawable_bpp * drawable_w);

      mod->ApplySubpixelGeometryDistortion (0, i, drawable_w, 1, undist_coord);

      gfloat *undist_iter   = undist_coord;
      guchar *output_buffer = &img_buf_out[drawable_bpp * drawable_w * i];

      // Iterate through subpixels in one row
      for (gint j = 0; j < drawable_w * drawable_bpp; j += drawable_bpp)
      {
        *output_buffer = interpolate_lanczos (img_buf,
                                              drawable_w,
                                              drawable_h,
                                              drawable_bpp,
                                              undist_iter[0],
                                              undist_iter[1],
                                              0);
        output_buffer++;
        *output_buffer = interpolate_lanczos (img_buf,
                                              drawable_w,
                                              drawable_h,
                                              drawable_bpp,
                                              undist_iter[2],
                                              undist_iter[3],
                                              1);
        output_buffer++;
        *output_buffer = interpolate_lanczos (img_buf,
                                              drawable_w,
                                              drawable_h,
                                              drawable_bpp,
                                              undist_iter[4],
                                              undist_iter[5],
                                              2);
        output_buffer++;

        // Move pointer to next pixel
        undist_iter += 2 * 3;
      }
#pragma omp atomic
      row_count++;

      // Update progress bar only every N rows
      if (row_count % 200 == 0)
      {
#pragma omp critical
        {
          gimp_progress_update ((gdouble) row_count / (gdouble) (drawable_h));
        }
      }
    }
    g_free (undist_coord);
  }

  delete mod;

#ifdef POSIX
  if (DEBUG) {
      clock_gettime(CLOCK_REALTIME, &profiling_stop);
      gulong time_diff =
        time_spec_to_gulong (&profiling_stop) -
        time_spec_to_gulong (&profiling_start);
      g_print("\nPerformance: %12llu ns, %d pixel -> %llu ns/pixel\n",
              time_diff,
              drawable_w * drawable_h,
              time_diff / (drawable_w * drawable_h));
  }
#endif

  // Write data back to gimp
  gegl_buffer_set (w_buffer,
                   GEGL_RECTANGLE (0, 0, drawable_w, drawable_h),
                   0,
                   drawable_fmt,
                   img_buf_out,
                   GEGL_AUTO_ROWSTRIDE);

  // Free memory
  g_object_unref (r_buffer);
  g_object_unref (w_buffer);

  gimp_drawable_merge_shadow (drawable_id, true);
  gimp_drawable_update (drawable_id, 0, 0, drawable_w, drawable_h);
  gimp_displays_flush ();

  g_free (img_buf_out);
  g_free (img_buf);

  lf_free (lenses);
  lf_free (cameras);
}

// ############################################################################
// Read camera and lens info from exif and try to find in database

static gint
read_opts_from_exif (gint32 image_id)
{
  Exiv2::ExifData exif_data;

  const lfCamera **cameras = nullptr;
  const lfCamera *camera   = nullptr;

  const lfLens **lenses = nullptr;
  const lfLens *lens    = nullptr;
  string       lens_name_mn;

  GExiv2Metadata *gimp_metadata;
  gchar          **exif_tags;

  if (DEBUG)
    g_print ("Reading exif data...");

  gimp_metadata = (GExiv2Metadata *) gimp_image_get_metadata (image_id);

  if (!gimp_metadata || !gexiv2_metadata_has_exif (gimp_metadata))
  {
    if (DEBUG)
      g_print ("No exif data found! \n");
    return -1;
  }
  else
  {
    if (DEBUG)
    {
      g_print ("OK.\nChecking for required exif tags...\n");
    }

    if (!gexiv2_metadata_has_tag (gimp_metadata, "Exif.Image.Make"))
    {
      if (DEBUG)
        g_print ("\t\'Exif.Image.Make\' not present!\n");
      return -1;
    }
    if (!gexiv2_metadata_has_tag (gimp_metadata, "Exif.Image.Model"))
    {
      if (DEBUG)
        g_print ("\t\'Exif.Image.Model\' not present!\n");
      return -1;
    }
    if (!gexiv2_metadata_has_tag (gimp_metadata, "Exif.Photo.FocalLength"))
    {
      if (DEBUG)
        g_print ("\t\'Exif.Photo.FocalLength\' not present!\n");
      return -1;
    }
    if (!gexiv2_metadata_has_tag (gimp_metadata, "Exif.Photo.FNumber"))
    {
      if (DEBUG)
        g_print ("\t\'Exif.Photo.FNumber\' not present!\n");
      return -1;
    }
  }

  exif_tags = gexiv2_metadata_get_exif_tags (gimp_metadata);

  while (*exif_tags != nullptr)
  {
    exif_data[*exif_tags] =
      gexiv2_metadata_get_tag_string (gimp_metadata, *exif_tags);
    exif_tags++;
  }

  // Search database for camera
  cameras =
    ldb->FindCameras (exif_data["Exif.Image.Make"].toString ().c_str (),
                      exif_data["Exif.Image.Model"].toString ().c_str ());
  if (cameras)
  {
    camera = cameras[0];
    lensfun_params.crop      = camera->CropFactor;
    lensfun_params.camera    = string (lf_mlstr_get (camera->Model));
    lensfun_params.cam_maker = string (lf_mlstr_get (camera->Maker));
  }
  else
  {
    lensfun_params.cam_maker = exif_data["Exif.Image.Make"].toString ();
  }

  //print_cameras(cameras, ldb);

  // Get lensID
  string cam_maker = exif_data["Exif.Image.Make"].toString ();
  transform (cam_maker.begin (),
             cam_maker.end (),
             cam_maker.begin (),
             ::tolower);
  string maker_note_key;

  // Select special MakerNote Tag for lens depending on Maker
  if ((cam_maker.find ("pentax")) != string::npos)
  {
    maker_note_key = "Exif.Pentax.LensType";
  }
  else if ((cam_maker.find ("canon")) != string::npos)
  {
    maker_note_key = "Exif.CanonCs.LensType";
  }
  else if ((cam_maker.find ("minolta")) != string::npos)
  {
    maker_note_key = "Exif.Minolta.LensID";
  }
  else if ((cam_maker.find ("nikon")) != string::npos)
  {
    maker_note_key = "Exif.NikonLd3.LensIDNumber";
    if (exif_data[maker_note_key].toString ().empty ())
    {
      maker_note_key = "Exif.NikonLd2.LensIDNumber";
    }
    if (exif_data[maker_note_key].toString ().empty ())
    {
      maker_note_key = "Exif.NikonLd1.LensIDNumber";
    }
  }
  else if ((cam_maker.find ("olympus")) != string::npos)
  {
    maker_note_key = "Exif.OlympusEq.LensType";
  }
  else
  {
    // Use default lens model tag for all other makers
    maker_note_key = "Exif.Photo.LensModel";
  }

  if (!gexiv2_metadata_has_tag (gimp_metadata, maker_note_key.c_str ()))
  {
    if (DEBUG)
      g_print ("\t\'%s\' not present!\n", maker_note_key.c_str ());
    return -1;
  }
  else if (DEBUG)
  {
    g_print ("\tAll tags present.\n");
  }

  // Decode lens ID
  if (!maker_note_key.empty () &&
      !exif_data[maker_note_key].toString ().empty ())
  {
    Exiv2::ExifKey                  ek (maker_note_key);
    Exiv2::ExifData::const_iterator md = exif_data.findKey (ek);

    if (md != exif_data.end ())
    {
      lens_name_mn = md->print (&exif_data);

      // Modify some lens names for better searching in lfDatabase
      if (cam_maker.find ("nikon") != string::npos)
      {
        str_replace (lens_name_mn, "Nikon", "");
        str_replace (lens_name_mn, "Zoom-Nikkor", "");
      }
    }
  }

  if (camera)
  {
    if (lens_name_mn.size () > 8)
    {
      // Only take lens names with significant length
      lenses = ldb->FindLenses (camera, nullptr, lens_name_mn.c_str ());
    }
    else
    {
      lenses = ldb->FindLenses (camera, nullptr, nullptr);
    }
    if (lenses)
    {
      lens = lenses[0];
      lensfun_params.lens = string (lf_mlstr_get (lens->Model));
    }
    lf_free (lenses);
  }

  lf_free (cameras);

  lensfun_params.focal    = exif_data["Exif.Photo.FocalLength"].toFloat ();
  lensfun_params.aperture = exif_data["Exif.Photo.FNumber"].toFloat ();

  if (DEBUG)
  {
    g_print ("\nExif Data:\n");
    g_print ("\tcamera: %s, %s\n",
             lensfun_params.cam_maker.c_str (),
             lensfun_params.camera.c_str ());
    g_print ("\tlens: %s\n", lensfun_params.lens.c_str ());
    g_print ("\tfocal Length: %f\n", lensfun_params.focal);
    g_print ("\tF-Stop: %f\n", lensfun_params.aperture);
    g_print ("\tCrop Factor: %f\n", lensfun_params.crop);
    g_print ("\tscale: %f\n", lensfun_params.scale);
  }

  return 0;
}

// ############################################################################
// Store and load parameters and settings to/from gimp_data_storage

static void
load_settings ()
{
  gimp_get_data ("plug-in-gimplensfun", &lensfun_param_storage);

  lensfun_param_storage.modify_flags = lensfun_param_storage.modify_flags;
  lensfun_param_storage.inverse      = lensfun_param_storage.inverse;
  if (strlen (lensfun_param_storage.camera) > 0)
    lensfun_params.camera = string (lensfun_param_storage.camera);
  if (strlen (lensfun_param_storage.cam_maker) > 0)
    lensfun_params.cam_maker = string (lensfun_param_storage.cam_maker);
  if (strlen (lensfun_param_storage.lens) > 0)
    lensfun_params.lens = string (lensfun_param_storage.lens);
  lensfun_param_storage.scale        = lensfun_param_storage.scale;
  lensfun_param_storage.crop         = lensfun_param_storage.crop;
  lensfun_param_storage.focal        = lensfun_param_storage.focal;
  lensfun_param_storage.aperture     = lensfun_param_storage.aperture;
  lensfun_param_storage.distance     = lensfun_param_storage.distance;
  lensfun_param_storage.target_geom  = lensfun_param_storage.target_geom;
}
//--------------------------------------------------------------------

static void
store_settings ()
{
  lensfun_param_storage.modify_flags = lensfun_param_storage.modify_flags;
  lensfun_param_storage.inverse      = lensfun_param_storage.inverse;
  strcpy (lensfun_param_storage.camera, lensfun_params.camera.c_str ());
  strcpy (lensfun_param_storage.cam_maker, lensfun_params.cam_maker.c_str ());
  strcpy (lensfun_param_storage.lens, lensfun_params.lens.c_str ());
  lensfun_param_storage.scale       = lensfun_param_storage.scale;
  lensfun_param_storage.crop        = lensfun_param_storage.crop;
  lensfun_param_storage.focal       = lensfun_param_storage.focal;
  lensfun_param_storage.aperture    = lensfun_param_storage.aperture;
  lensfun_param_storage.distance    = lensfun_param_storage.distance;
  lensfun_param_storage.target_geom = lensfun_param_storage.target_geom;

  gimp_set_data ("plug-in-gimplensfun",
                 &lensfun_param_storage,
                 sizeof (lensfun_param_storage));
}

// ############################################################################
// Run()

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam        **return_vals)
{
  static GimpParam values[1];
  gint32           image_id;
  GimpRunMode      run_mode;
  gint32           drawable_id;

  gegl_init (nullptr, nullptr);

  // Setting mandatory output values
  *nreturn_vals = 1;
  *return_vals  = values;

  run_mode = (GimpRunMode) param[0].data.d_int32;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_SUCCESS;

  gimp_progress_init ("Lensfun correction...");

  image_id    = param[1].data.d_drawable;
  drawable_id = param[2].data.d_drawable;

  if (DEBUG)
    g_print ("Loading database... ");

  //Load lensfun database
  ldb = new lfDatabase ();
  if (ldb->Load () != LF_NO_ERROR)
  {
    if (DEBUG)
      g_print ("failed!\n");
    return;
  }
  else
  {
    if (DEBUG)
      g_print ("OK\n");
  }

  // Read exif data
  if (read_opts_from_exif (image_id) != 0)
    load_settings ();

  if (run_mode == GIMP_RUN_INTERACTIVE)
  {
    if (DEBUG)
      g_print ("Creating dialog...\n");
    // Display the dialog
    if (create_dialog_window ())
      process_image (drawable_id);
  }
  else
  {
    /* If run_mode is non-interactive, we use the configuration
     * from read_opts_from_exif. If that fails, we use the stored settings
     * (load_settings()), e.g. the settings that have been made in the last
     * interactive use of the plugin. One day, all settings should be
     * available as arguments in non-interactive mode.
     */
    process_image (drawable_id);
  }

  store_settings ();
  gegl_exit ();

  delete ldb;
}
