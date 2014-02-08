/*
 *
 *
 Copyright 2010-2011 Sebastian Kraft

 This file is part of GimpLensfun.

 GimpLensfun is free software: you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation, either version
 3 of the License, or (at your option) any later version.

 GimpLensfun is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied
 warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 PURPOSE. See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public
 License along with GimpLensfun. If not, see
 http://www.gnu.org/licenses/.
 
*/

#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>
#include <float.h>

#include <lensfun.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include <exiv2/image.hpp>
#include <exiv2/exif.hpp>

#define VERSIONSTR "0.2.4-dev"


#ifndef DEBUG
#define DEBUG 0
#endif

#include "LUT.hpp"

using namespace std;

//####################################################################
// Function declarations
static void query (void);
static void run   (const gchar      *name,
                   gint              nparams,
                   const GimpParam  *param,
                   gint             *nreturn_vals,
                   GimpParam       **return_vals);

static gboolean create_dialog_window (GimpDrawable *drawable);
//--------------------------------------------------------------------


//####################################################################
// Global variables
GtkWidget *camera_combo, *maker_combo, *lens_combo;
GtkWidget *CorrVignetting, *CorrTCA, *CorrDistortion;
lfDatabase *ldb;
bool bComboBoxLock = false;
//--------------------------------------------------------------------


//####################################################################
// interpolation parameters
const int cLanczosWidth = 2;
const int cLanczosTableRes = 256;
LUT<float> LanczosLUT (cLanczosWidth * 2 * cLanczosTableRes + 1);

typedef enum GL_INTERPOL {
    GL_INTERPOL_NN,		// Nearest Neighbour
    GL_INTERPOL_BL,		// Bilinear
    GL_INTERPOL_LZ		// Lanczos
} glInterpolationType;


//####################################################################
// List of camera makers
const string    CameraMakers[] = {
    "Canon",
    "Casio",
    "Fujifilm",
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
    "NULL"
};
//--------------------------------------------------------------------


//####################################################################
// struct for holding camera/lens info and parameters
typedef struct
{
    int ModifyFlags;
    bool Inverse;
    std::string Camera;
    std::string CamMaker;
    std::string Lens;
    float Scale;
    float Crop;
    float Focal;
    float Aperture;
    float Distance;
    lfLensType TargetGeom;
} MyLensfunOpts;
//--------------------------------------------------------------------
static MyLensfunOpts sLensfunParameters =
{
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
//--------------------------------------------------------------------


//####################################################################
// struct for storing camera/lens info and parameters
typedef struct
{
    int ModifyFlags;
    bool Inverse;
    char Camera[255];
    char CamMaker[255];
    char Lens[255];
    float Scale;
    float Crop;
    float Focal;
    float Aperture;
    float Distance;
    lfLensType TargetGeom;
} MyLensfunOptStorage;
//--------------------------------------------------------------------
static MyLensfunOptStorage sLensfunParameterStorage =
{
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


// GIMP Plugin
GimpPlugInInfo PLUG_IN_INFO =
{
    NULL,
    NULL,
    query,
    run
};

MAIN()


//####################################################################
// query() function
static void  query (void)
{
    static GimpParamDef args[] =
    {
        {
            GIMP_PDB_INT32,
            "run-mode",
            "Run mode"
        },
        {
            GIMP_PDB_IMAGE,
            "image",
            "Input image"
        },
        {
            GIMP_PDB_DRAWABLE,
            "drawable",
            "Input drawable"
        }
    };

    gimp_install_procedure (
        "plug-in-lensfun",
        "Correct lens distortion with lensfun",
        "Correct lens distortion with lensfun",
        "Sebastian Kraft",
        "Copyright Sebastian Kraft",
        "2010",
        "_GimpLensfun...",
        "RGB",
        GIMP_PLUGIN,
        G_N_ELEMENTS (args), 0,
        args, NULL);

    gimp_plugin_menu_register ("plug-in-lensfun",
                               "<Image>/Filters/Enhance");
}
//--------------------------------------------------------------------


//####################################################################
// Some helper functions

// Round float to integer value
int roundfloat2int(float d)
{
    return d<0?d-.5:d+.5;
}
//--------------------------------------------------------------------
void StrReplace(std::string& str, const std::string& old, const std::string& newstr)
{
    size_t pos = 0;
    while ((pos = str.find(old, pos)) != std::string::npos)
    {
        str.replace(pos, old.length(), newstr);
        pos += newstr.length();
    }
}
//--------------------------------------------------------------------
int StrCompare(const std::string& str1, const std::string& str2, bool CaseSensitive = false)
{
    string s1 = str1;
    string s2 = str2;

    if (!CaseSensitive)
    {
        transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
        transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
    }

    return s1.compare(s2);

}
//--------------------------------------------------------------------
#ifdef POSIX
unsigned long long int timespec2llu(struct timespec *ts) {
    return (unsigned long long int) ( ((unsigned long long int)ts->tv_sec * 1000000000) + ts->tv_nsec);
}
#endif
//--------------------------------------------------------------------



//####################################################################
// Helper functions for printing debug output
static void PrintMount (const lfMount *mount)
{
    g_print ("Mount: %s\n", lf_mlstr_get (mount->Name));
    if (mount->Compat)
        for (int j = 0; mount->Compat [j]; j++)
            g_print ("\tCompat: %s\n", mount->Compat [j]);
}
//--------------------------------------------------------------------
static void PrintCamera (const lfCamera *camera)
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
//--------------------------------------------------------------------
static void PrintLens (const lfLens *lens)
{
    g_print ("Lens: %s / %s\n",
             lf_mlstr_get (lens->Maker),
             lf_mlstr_get (lens->Model));
    g_print ("\tCrop factor: %g\n", lens->CropFactor);
    g_print ("\tFocal: %g-%g\n", lens->MinFocal, lens->MaxFocal);
    g_print ("\tAperture: %g-%g\n", lens->MinAperture, lens->MaxAperture);
    g_print ("\tCenter: %g,%g\n", lens->CenterX, lens->CenterY);
    g_print ("\tCCI: %g/%g/%g\n", lens->RedCCI, lens->GreenCCI, lens->BlueCCI);
    if (lens->Mounts)
        for (int j = 0; lens->Mounts [j]; j++)
            g_print ("\tMount: %s\n", lf_db_mount_name (ldb, lens->Mounts [j]));
}
//--------------------------------------------------------------------
static void PrintCameras (const lfCamera **cameras)
{
    if (cameras)
        for (int i = 0; cameras [i]; i++)
        {
            g_print ("--- camera %d: ---\n", i + 1);
            PrintCamera (cameras [i]);
        }
    else
        g_print ("\t- failed\n");
}
//--------------------------------------------------------------------
static void PrintLenses (const lfLens **lenses)
{
    if (lenses)
        for (int i = 0; lenses [i]; i++)
        {
            g_print ("--- lens %d, score %d: ---\n", i + 1, lenses [i]->Score);
            PrintLens (lenses [i]);
        }
    else
        g_print ("\t- failed\n");
}
//--------------------------------------------------------------------


//####################################################################
// set dialog combo boxes to values
static void dialog_set_cboxes( string sNewMake, string sNewCamera, string sNewLens) {

    vector<string> vCameraList;
    vector<string> vLensList;

    const lfCamera**    cameras     = NULL;
    const lfLens**  lenses      = NULL;
    GtkTreeModel*   store       = NULL;

    int iCurrMakerID    = -1;
    int iCurrCameraID   = -1;
    int iCurrLensId     = -1;

    sLensfunParameters.CamMaker.clear();
    sLensfunParameters.Camera.clear();
    sLensfunParameters.Lens.clear();

    if (sNewMake.empty()==true)
            return;

    // try to match maker with predefined list
    for (int i = 0; CameraMakers[i].compare("NULL")!=0; i++)
    {
        if (StrCompare(CameraMakers[i], sNewMake)==0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(maker_combo), i);
            iCurrMakerID = i;
        }
    }

    // return if no maker has been identified
    if (iCurrMakerID==-1)
        return;

    // store cam maker
    sLensfunParameters.CamMaker = CameraMakers[iCurrMakerID];

    // clear camera/lens combobox
    store = gtk_combo_box_get_model( GTK_COMBO_BOX(camera_combo) );
    gtk_list_store_clear( GTK_LIST_STORE( store ) );
    store = gtk_combo_box_get_model( GTK_COMBO_BOX(lens_combo) );
    gtk_list_store_clear( GTK_LIST_STORE( store ) );

    // get all cameras from maker out of database
    cameras = ldb->FindCamerasExt (CameraMakers[iCurrMakerID].c_str(), NULL, LF_SEARCH_LOOSE );
    if (cameras) {
        for (int i=0; cameras [i]; i++){
            vCameraList.push_back(string(lf_mlstr_get(cameras[i]->Model)));
        }
        sort(vCameraList.begin(), vCameraList.end());
    } else {
        return;
    }

    for (unsigned int i=0; i<vCameraList.size(); i++)
    {
        gtk_combo_box_append_text( GTK_COMBO_BOX( camera_combo ), vCameraList[i].c_str());
        // set to active if model matches current camera
        if ((!sNewCamera.empty()) && (StrCompare(sNewCamera, vCameraList[i])==0)) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(camera_combo), i);
            sLensfunParameters.Camera = sNewCamera;
        }

        if (StrCompare(string(lf_mlstr_get(cameras[i]->Model)), sNewCamera)==0)
            iCurrCameraID = i;
    }

    // return if camera is unidentified
    if (iCurrCameraID == -1)
    {
        lf_free(cameras);
        return;
    }

    // find lenses for camera model
    lenses = ldb->FindLenses (cameras[iCurrCameraID], NULL, NULL);
    if (lenses) {
        vLensList.clear();
        for (int i = 0; lenses [i]; i++)
            vLensList.push_back(string(lf_mlstr_get(lenses[i]->Model)));
        sort(vLensList.begin(), vLensList.end());
    } else {
        lf_free(cameras);
        return;
    }

    for (unsigned int i = 0; i<vLensList.size(); i++)
    {
        gtk_combo_box_append_text( GTK_COMBO_BOX( lens_combo ), (vLensList[i]).c_str());

        // set active if lens matches current lens model
        if ((!sNewLens.empty()) && (StrCompare(sNewLens, vLensList[i])==0)) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(lens_combo), i);
            sLensfunParameters.Lens = sNewLens;
        }

        if (StrCompare(string(lf_mlstr_get(lenses[i]->Model)), sNewLens)==0)
            iCurrLensId = i;
    }

    gtk_widget_set_sensitive(CorrTCA, false);
    gtk_widget_set_sensitive(CorrVignetting, false);

    if (iCurrLensId >= 0)
    {
        if (lenses[iCurrLensId]->CalibTCA != NULL)
            gtk_widget_set_sensitive(CorrTCA, true);
        if (lenses[iCurrLensId]->CalibVignetting != NULL)
            gtk_widget_set_sensitive(CorrVignetting, true);
    }

    lf_free(lenses);
    lf_free(cameras);
}
//--------------------------------------------------------------------

//####################################################################
// dialog callback functions
static void
maker_cb_changed( GtkComboBox *combo,
                  gpointer     data )
{
    if (!bComboBoxLock) {
        bComboBoxLock = true;
        dialog_set_cboxes(gtk_combo_box_get_active_text(GTK_COMBO_BOX(maker_combo)),
                          "",
                          "");
        bComboBoxLock = false;
    }
}
//--------------------------------------------------------------------
static void
camera_cb_changed( GtkComboBox *combo,
                   gpointer     data )
{
    if (!bComboBoxLock) {
        bComboBoxLock = true;
        dialog_set_cboxes(string(gtk_combo_box_get_active_text(GTK_COMBO_BOX(maker_combo))),
                          string(gtk_combo_box_get_active_text(GTK_COMBO_BOX(camera_combo))),
                          "");
        bComboBoxLock = false;
    }
}
//--------------------------------------------------------------------
static void
lens_cb_changed( GtkComboBox *combo,
                 gpointer     data )
{
    if (!bComboBoxLock) {
        bComboBoxLock = true;
        dialog_set_cboxes(string(gtk_combo_box_get_active_text(GTK_COMBO_BOX(maker_combo))),
                          string(gtk_combo_box_get_active_text(GTK_COMBO_BOX(camera_combo))),
                          string(gtk_combo_box_get_active_text(GTK_COMBO_BOX(lens_combo))));
        bComboBoxLock = false;
    }
}
//--------------------------------------------------------------------
static void
focal_changed( GtkComboBox *combo,
               gpointer     data )
{
    sLensfunParameters.Focal = (float) gtk_adjustment_get_value(GTK_ADJUSTMENT(data));
}
//--------------------------------------------------------------------
static void
aperture_changed( GtkComboBox *combo,
               gpointer     data )
{
    sLensfunParameters.Aperture = (float) gtk_adjustment_get_value(GTK_ADJUSTMENT(data));
}
//--------------------------------------------------------------------
static void
scalecheck_changed( GtkCheckButton *togglebutn,
                    gpointer     data )
{
    sLensfunParameters.Scale = !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(togglebutn));
}
//--------------------------------------------------------------------
static void
modify_changed( GtkCheckButton *togglebutn,
                    gpointer     data )
{
    sLensfunParameters.ModifyFlags = 0;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(CorrDistortion))
        && GTK_WIDGET_SENSITIVE(CorrDistortion))
        sLensfunParameters.ModifyFlags |= LF_MODIFY_DISTORTION;

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(CorrTCA))
        && GTK_WIDGET_SENSITIVE(CorrTCA))
        sLensfunParameters.ModifyFlags |= LF_MODIFY_TCA;

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(CorrVignetting))
        && GTK_WIDGET_SENSITIVE(CorrVignetting))
        sLensfunParameters.ModifyFlags |= LF_MODIFY_VIGNETTING;
}//--------------------------------------------------------------------


//####################################################################
// Create gtk dialog window
static gboolean create_dialog_window (GimpDrawable *drawable)
{
    GtkWidget *dialog;
    GtkWidget *main_vbox;
    GtkWidget *frame, *frame2;
    GtkWidget *camera_label, *lens_label, *maker_label;
    GtkWidget *focal_label, *aperture_label;
    GtkWidget *scalecheck;

    GtkWidget *spinbutton;
    GtkObject *spinbutton_adj;
    GtkWidget *spinbutton_aperture;
    GtkObject *spinbutton_aperture_adj;
    GtkWidget *frame_label, *frame_label2;
    GtkWidget *table, *table2;
    gboolean   run;

    gint       iTableRow = 0;

    gimp_ui_init ("mylensfun", FALSE);

    dialog = gimp_dialog_new ("GIMP-Lensfun (v" VERSIONSTR ")", "mylensfun",
                              NULL, GTK_DIALOG_MODAL ,
                              gimp_standard_help_func, "plug-in-lensfun",
                              GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                              GTK_STOCK_OK,     GTK_RESPONSE_OK,
                              NULL);

    main_vbox = gtk_vbox_new (FALSE, 6);
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), main_vbox);
    gtk_widget_show (main_vbox);

    frame = gtk_frame_new (NULL);
    gtk_widget_show (frame);
    gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 6);

    frame_label = gtk_label_new ("Camera/Lens Parameters");
    gtk_widget_show (frame_label);
    gtk_frame_set_label_widget (GTK_FRAME (frame), frame_label);
    gtk_label_set_use_markup (GTK_LABEL (frame_label), TRUE);

    table = gtk_table_new(6, 2, TRUE);
    gtk_table_set_homogeneous(GTK_TABLE(table), false);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 10);

    // camera maker
    maker_label = gtk_label_new ("Maker:");
    gtk_misc_set_alignment(GTK_MISC(maker_label),0.0,0.5);
    gtk_widget_show (maker_label);
    gtk_table_attach(GTK_TABLE(table), maker_label, 0, 1, iTableRow, iTableRow+1, GTK_FILL, GTK_FILL, 0,0 );

    maker_combo = gtk_combo_box_new_text();
    gtk_widget_show (maker_combo);

    for (int i = 0; StrCompare(CameraMakers[i], "NULL")!=0; i++)
    {
        gtk_combo_box_append_text( GTK_COMBO_BOX( maker_combo ), CameraMakers[i].c_str());
    }

    gtk_table_attach_defaults(GTK_TABLE(table), maker_combo, 1, 2, iTableRow, iTableRow+1 );

    iTableRow++;

    // camera
    camera_label = gtk_label_new ("Camera:");
    gtk_misc_set_alignment(GTK_MISC(camera_label),0.0,0.5);
    gtk_widget_show (camera_label);
    gtk_table_attach(GTK_TABLE(table), camera_label, 0, 1, iTableRow, iTableRow+1, GTK_FILL, GTK_FILL, 0,0 );

    camera_combo = gtk_combo_box_new_text();
    gtk_widget_show (camera_combo);

    gtk_table_attach_defaults(GTK_TABLE(table), camera_combo, 1,2, iTableRow, iTableRow+1 );

    iTableRow++;

    // lens
    lens_label = gtk_label_new ("Lens:");
    gtk_misc_set_alignment(GTK_MISC(lens_label),0.0,0.5);
    gtk_widget_show (lens_label);
    gtk_table_attach_defaults(GTK_TABLE(table), lens_label, 0,1,iTableRow, iTableRow+1 );

    lens_combo = gtk_combo_box_new_text();
    gtk_widget_show (lens_combo);

    gtk_table_attach_defaults(GTK_TABLE(table), lens_combo, 1,2,iTableRow, iTableRow+1 );
    iTableRow++;

    // focal length
    focal_label = gtk_label_new("Focal length (mm):");
    gtk_misc_set_alignment(GTK_MISC(focal_label),0.0,0.5);
    gtk_widget_show (focal_label);
    gtk_table_attach_defaults(GTK_TABLE(table), focal_label, 0,1,iTableRow, iTableRow+1 );

    spinbutton_adj = gtk_adjustment_new (sLensfunParameters.Focal, 0, 5000, 0.1, 0, 0);
    spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (spinbutton_adj), 2, 1);
    gtk_widget_show (spinbutton);
    gtk_table_attach_defaults(GTK_TABLE(table), spinbutton, 1,2,iTableRow, iTableRow+1 );
    iTableRow++;

    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinbutton), TRUE);

    // aperture
    aperture_label = gtk_label_new("Aperture:");
    gtk_misc_set_alignment(GTK_MISC(aperture_label),0.0,0.5);
    gtk_widget_show (focal_label);
    gtk_table_attach_defaults(GTK_TABLE(table), aperture_label, 0,1,iTableRow, iTableRow+1 );

    spinbutton_aperture_adj = gtk_adjustment_new (sLensfunParameters.Aperture, 0, 128, 0.1, 0, 0);
    spinbutton_aperture = gtk_spin_button_new (GTK_ADJUSTMENT (spinbutton_aperture_adj), 2, 1);
    gtk_widget_show (spinbutton_aperture);
    gtk_table_attach_defaults(GTK_TABLE(table), spinbutton_aperture, 1,2,iTableRow, iTableRow+1 );
    iTableRow++;

    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinbutton_aperture), TRUE);

    gtk_container_add (GTK_CONTAINER (frame), table);
    gtk_widget_show_all(table);

    frame2 = gtk_frame_new (NULL);
    gtk_widget_show (frame2);
    gtk_box_pack_start (GTK_BOX (main_vbox), frame2, TRUE, TRUE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (frame2), 6);

    frame_label2 = gtk_label_new ("Processing Parameters");
    gtk_widget_show (frame_label2);
    gtk_frame_set_label_widget (GTK_FRAME (frame2), frame_label2);
    gtk_label_set_use_markup (GTK_LABEL (frame_label2), TRUE);

    table2 = gtk_table_new(6, 2, TRUE);
    gtk_table_set_homogeneous(GTK_TABLE(table2), false);
    gtk_table_set_row_spacings(GTK_TABLE(table2), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table2), 2);
    gtk_container_set_border_width(GTK_CONTAINER(table2), 10);

    iTableRow = 0;

    // scale to fit checkbox
    scalecheck = gtk_check_button_new_with_label("Scale to fit");
    //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), FALSE);
    gtk_widget_show (scalecheck);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scalecheck), !sLensfunParameters.Scale);
    gtk_table_attach_defaults(GTK_TABLE(table2), scalecheck, 1,2,iTableRow, iTableRow+1 );
    iTableRow++;

    // enable distortion correction
    CorrDistortion = gtk_check_button_new_with_label("Distortion");
    //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), FALSE);
    gtk_widget_show (CorrDistortion);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(CorrDistortion), true);
    gtk_table_attach_defaults(GTK_TABLE(table2), CorrDistortion, 1,2,iTableRow, iTableRow+1 );
    iTableRow++;

    // enable vignetting correction
    CorrVignetting = gtk_check_button_new_with_label("Vignetting");
    //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), FALSE);
    gtk_widget_show (CorrVignetting);
    gtk_widget_set_sensitive(CorrVignetting, false);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(CorrVignetting), false);
    gtk_table_attach_defaults(GTK_TABLE(table2), CorrVignetting, 1,2,iTableRow, iTableRow+1 );
    iTableRow++;

    // enable TCA correction
    CorrTCA = gtk_check_button_new_with_label("Chromatic Aberration");
    //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), FALSE);
    gtk_widget_show (CorrTCA);
    gtk_widget_set_sensitive(CorrTCA, false);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(CorrTCA), false);
    gtk_table_attach_defaults(GTK_TABLE(table2), CorrTCA, 1,2,iTableRow, iTableRow+1 );
    iTableRow++;

    gtk_container_add (GTK_CONTAINER (frame2), table2);
    gtk_widget_show_all(table2);

    // try to set combo boxes to exif
    dialog_set_cboxes(sLensfunParameters.CamMaker,
                      sLensfunParameters.Camera,
                      sLensfunParameters.Lens);

    // connect signals
    g_signal_connect( G_OBJECT( maker_combo ), "changed",
                      G_CALLBACK( maker_cb_changed ), NULL );
    g_signal_connect( G_OBJECT( camera_combo ), "changed",
                      G_CALLBACK( camera_cb_changed ), NULL );
    g_signal_connect( G_OBJECT( lens_combo ), "changed",
                      G_CALLBACK( lens_cb_changed ), NULL );
    g_signal_connect (spinbutton_adj, "value_changed",
                      G_CALLBACK (focal_changed), spinbutton_adj);
    g_signal_connect (spinbutton_aperture_adj, "value_changed",
                      G_CALLBACK (aperture_changed), spinbutton_aperture_adj);

    g_signal_connect( G_OBJECT( scalecheck ), "toggled",
                      G_CALLBACK( scalecheck_changed ), NULL );
    g_signal_connect( G_OBJECT( CorrDistortion ), "toggled",
                      G_CALLBACK( modify_changed ), NULL );
    g_signal_connect( G_OBJECT( CorrTCA ), "toggled",
                      G_CALLBACK( modify_changed ), NULL );
    g_signal_connect( G_OBJECT( CorrVignetting ), "toggled",
                      G_CALLBACK( modify_changed ), NULL );

    // show and run
    gtk_widget_show (dialog);
    run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

    gtk_widget_destroy (dialog);
    return run;
}
//--------------------------------------------------------------------


//####################################################################
// Interpolation functions
inline float Lanczos(float x)
{
    if ( (x<FLT_MIN) && (x>-FLT_MIN) )
        return 1.0f;

    if ( (x >= cLanczosWidth) && (x <= (-1)*cLanczosWidth) )
        return 0.0f;

    float xpi = x * static_cast<float>(M_PI);
    return ( cLanczosWidth * sin(xpi) * sin(xpi/cLanczosWidth) ) / ( xpi*xpi );
}
//--------------------------------------------------------------------
void InitInterpolation(glInterpolationType intType)
{
    switch(intType) {
        case GL_INTERPOL_NN: break;
        case GL_INTERPOL_BL: break;
        case GL_INTERPOL_LZ:
                for (int i = -cLanczosWidth*cLanczosTableRes; i < cLanczosWidth*cLanczosTableRes; i++) {
                    LanczosLUT[i + cLanczosWidth*cLanczosTableRes] = Lanczos(static_cast<float>(i)/static_cast<float>(cLanczosTableRes));
                }

                break;
    }
}
//--------------------------------------------------------------------
inline int InterpolateLanczos(guchar *ImgBuffer, gint w, gint h, gint channels, float xpos, float ypos, int chan)
{

    int   xl   = int(xpos);
    int   yl   = int(ypos);
    float y    = 0.0f;
    float norm = 0.0f;
    float L    = 0.0f;

    // border checking
    if ((xl-cLanczosWidth+1 < 0) ||
        (xl+cLanczosWidth >= w)  ||
        (yl-cLanczosWidth+1 < 0) ||
        (yl+cLanczosWidth >= h))
    {
        return 0;
    }

    // convolve with lanczos kernel
    for (int i = xl-cLanczosWidth+1; i < xl+cLanczosWidth; i++) {
        for (int j = yl-cLanczosWidth+1; j < yl+cLanczosWidth; j++) {
            L = LanczosLUT[ (xpos - static_cast<float>(i))*static_cast<float>(cLanczosTableRes) + static_cast<float>(cLanczosWidth*cLanczosTableRes) ]
                   * LanczosLUT[ (ypos - static_cast<float>(j))*static_cast<float>(cLanczosTableRes) + static_cast<float>(cLanczosWidth*cLanczosTableRes) ];
            // L = Lanczos(xpos - static_cast<float>(i))
            //                   * Lanczos(ypos - static_cast<float>(j));
            y += static_cast<float>(ImgBuffer[ (channels*w*j) + (i*channels) + chan ]) * L;
            norm += L;
        }
    }
    // normalize
    y = y / norm;

    // clip
    if (y>255)
        y = 255;
    if (y<0)
        y = 0;

    // round to integer and return
    return roundfloat2int(y);
}
//--------------------------------------------------------------------
inline int InterpolateLinear(guchar *ImgBuffer, gint w, gint h, gint channels, float xpos, float ypos, int chan)
{
    // interpolated values in x and y  direction
    float   x1, x2, y;

    // surrounding integer rounded coordinates
    int     xl, xr, yu, yl;

    xl = floor(xpos);
    xr = ceil (xpos + 1e-10);
    yu = floor(ypos);
    yl = ceil (ypos + 1e-10);

    // border checking
    if ((xl < 0)  ||
        (xr >= w) ||
        (yu < 0)  ||
        (yl >= h))
    {
        return 0;
    }


    float px1y1 = (float) ImgBuffer[ (channels*w*yu) + (xl*channels) + chan ];
    float px1y2 = (float) ImgBuffer[ (channels*w*yl) + (xl*channels) + chan ];
    float px2y1 = (float) ImgBuffer[ (channels*w*yu) + (xr*channels) + chan ];
    float px2y2 = (float) ImgBuffer[ (channels*w*yl) + (xr*channels) + chan ];

    x1 = (static_cast<float>(xr) - xpos)*px1y1 + (xpos - static_cast<float>(xl))*px2y1;
    x2 = (static_cast<float>(xr) - xpos)*px1y2 + (xpos - static_cast<float>(xl))*px2y2;

    y  = (ypos - static_cast<float>(yu))*x2    + (static_cast<float>(yl) - ypos)*x1;

    return roundfloat2int(y);
}
//--------------------------------------------------------------------
inline int InterpolateNearest(guchar *ImgBuffer, gint w, gint h, gint channels, float xpos, float ypos, int chan)
{
    int x = roundfloat2int(xpos);
    int y = roundfloat2int(ypos);


    // border checking
    if ((x < 0)  ||
        (x >= w) ||
        (y < 0)  ||
        (y >= h))
    {
        return 0;
    }

    return ImgBuffer[ (channels*w*y) + (x*channels) + chan ];
}
//--------------------------------------------------------------------


//####################################################################
// Processing
static void process_image (GimpDrawable *drawable) {
    gint         channels;
    gint         x1, y1, x2, y2, imgwidth, imgheight;

    GimpPixelRgn rgn_in, rgn_out;
    guchar *ImgBuffer;
    guchar *ImgBufferOut;

    #ifdef POSIX
    struct timespec profiling_start, profiling_stop;
    #endif

    // get image size
    gimp_drawable_mask_bounds (drawable->drawable_id,
                               &x1, &y1,
                               &x2, &y2);
    imgwidth = x2-x1;
    imgheight = y2-y1;

    // get number of channels
    channels = gimp_drawable_bpp (drawable->drawable_id);

    gimp_pixel_rgn_init (&rgn_in,
                         drawable,
                         x1, y1,
                         imgwidth, imgheight,
                         FALSE, FALSE);
    gimp_pixel_rgn_init (&rgn_out,
                         drawable,
                         x1, y1,
                         imgwidth, imgheight,
                         TRUE, TRUE);

    //Init input and output buffer
    ImgBuffer = g_new (guchar, channels * (imgwidth+1) * (imgheight+1));
    ImgBufferOut = g_new (guchar, channels * (imgwidth+1) * (imgheight+1));
    InitInterpolation(GL_INTERPOL_LZ);

    // Copy pixel data from GIMP to internal buffer
    gimp_pixel_rgn_get_rect (&rgn_in, ImgBuffer, x1, y1, imgwidth, imgheight);

    if (sLensfunParameters.Scale<1) {
        sLensfunParameters.ModifyFlags |= LF_MODIFY_SCALE;
    }

    const lfCamera **cameras = ldb->FindCamerasExt (sLensfunParameters.CamMaker.c_str(), sLensfunParameters.Camera.c_str());
    sLensfunParameters.Crop = cameras[0]->CropFactor;

    const lfLens **lenses = ldb->FindLenses (cameras[0], NULL, sLensfunParameters.Lens.c_str());

    if (DEBUG) {
        g_print("\nApplied settings:\n");
        g_print("\tCamera: %s, %s\n", cameras[0]->Maker, cameras[0]->Model);
        g_print("\tLens: %s\n", lenses[0]->Model);
        g_print("\tFocal Length: %f\n", sLensfunParameters.Focal);
        g_print("\tF-Stop: %f\n", sLensfunParameters.Aperture);
        g_print("\tCrop Factor: %f\n", sLensfunParameters.Crop);
        g_print("\tScale: %f\n", sLensfunParameters.Scale);

        #ifdef POSIX
        clock_gettime(CLOCK_REALTIME, &profiling_start);
        #endif
    }

    //init lensfun modifier
    lfModifier *mod = lfModifier::Create (lenses[0], sLensfunParameters.Crop, imgwidth, imgheight);
    mod->Initialize (  lenses[0], LF_PF_U8, sLensfunParameters.Focal,
                         sLensfunParameters.Aperture, sLensfunParameters.Distance, sLensfunParameters.Scale, sLensfunParameters.TargetGeom,
                         sLensfunParameters.ModifyFlags, sLensfunParameters.Inverse);

    int iRowCount = 0;
    #pragma omp parallel
    {
        // buffer containing undistorted coordinates for one row
        float *UndistCoord = g_new (float, imgwidth*2*channels);

        //main loop for processing, iterate through rows
        #pragma omp for
        for (int i = 0; i < imgheight; i++)
        {
            mod->ApplyColorModification( &ImgBuffer[(channels*imgwidth*i)],
                                        0, i, imgwidth, 1,
                                        LF_CR_3(RED, GREEN, BLUE),
                                        channels*imgwidth);

            mod->ApplySubpixelGeometryDistortion (0, i, imgwidth, 1, UndistCoord);

            float*  UndistIter = UndistCoord;
            guchar *OutputBuffer = &ImgBufferOut[channels*imgwidth*i];
            //iterate through subpixels in one row
            for (int j = 0; j < imgwidth*channels; j += channels)
            {
                *OutputBuffer = InterpolateLanczos(ImgBuffer, imgwidth, imgheight, channels, UndistIter [0], UndistIter [1], 0);
                OutputBuffer++;
                *OutputBuffer = InterpolateLanczos(ImgBuffer, imgwidth, imgheight, channels, UndistIter [2], UndistIter [3], 1);
                OutputBuffer++;
                *OutputBuffer = InterpolateLanczos(ImgBuffer, imgwidth, imgheight, channels, UndistIter [4], UndistIter [5], 2);
                OutputBuffer++;

                // move pointer to next pixel
                UndistIter += 2 * 3;
            }
            #pragma omp atomic
            iRowCount++;
            //update progress bar only every N rows
            if (iRowCount % 200 == 0) {
                 #pragma omp critical
                 {
                 gimp_progress_update ((gdouble) (iRowCount - y1) / (gdouble) (imgheight));
                 }
            }
        }
        g_free(UndistCoord);
    }

    lf_free(mod);

    #ifdef POSIX
    if (DEBUG) {
        clock_gettime(CLOCK_REALTIME, &profiling_stop);
        unsigned long long int time_diff = timespec2llu(&profiling_stop) - timespec2llu(&profiling_start);
        g_print("\nPerformance: %12llu ns, %d pixel -> %llu ns/pixel\n", time_diff, imgwidth*imgheight, time_diff / (imgwidth*imgheight));
    }
    #endif

    //write data back to gimp
    gimp_pixel_rgn_set_rect (&rgn_out, ImgBufferOut, x1, y1, imgwidth, imgheight);

    gimp_drawable_flush (drawable);
    gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
    gimp_drawable_update (drawable->drawable_id,
                          x1, y1,
                          imgwidth, imgheight);
    gimp_displays_flush ();
    gimp_drawable_detach (drawable);

    // free memory
    g_free(ImgBufferOut);
    g_free(ImgBuffer);

    lf_free(lenses);
    lf_free(cameras);
}
//--------------------------------------------------------------------


//####################################################################
// Read camera and lens info from exif and try to find in database
//
static int read_opts_from_exif(const char *filename) {

    Exiv2::Image::AutoPtr Exiv2image;
    Exiv2::ExifData exifData;

    const lfCamera  **cameras    = 0;
    const lfCamera  *camera      = 0;

    const lfLens    **lenses     = 0;
    const lfLens    *lens        = 0;
    std::string LensNameMN;

    if (DEBUG) {
        g_print ("Reading exif data...");
    }

    try {
        // read exif from file
        Exiv2image = Exiv2::ImageFactory::open(string(filename));
        Exiv2image.get();
        Exiv2image->readMetadata();
        exifData = Exiv2image->exifData();

        if (exifData.empty()) {
            if (DEBUG) {
                g_print ("no exif data found. \n");
            }
            return -1;
        }
    }
    catch (Exiv2::AnyError& e) {
        if (DEBUG) {
            g_print ("exception on reading data. \n");
        }
        return -1;
    }

    // search database for camera
    cameras = ldb->FindCameras (exifData["Exif.Image.Make"].toString().c_str(), exifData["Exif.Image.Model"].toString().c_str());
    if (cameras) {
        camera = cameras [0];
        sLensfunParameters.Crop = camera->CropFactor;
        sLensfunParameters.Camera = string(lf_mlstr_get(camera->Model));
        sLensfunParameters.CamMaker = string(lf_mlstr_get(camera->Maker));
    }  else {
        sLensfunParameters.CamMaker = exifData["Exif.Image.Make"].toString();
    }
    //PrintCameras(cameras, ldb);

    //Get lensID
    string CamMaker = exifData["Exif.Image.Make"].toString();
    transform(CamMaker.begin(), CamMaker.end(),CamMaker.begin(), ::tolower);
    string MakerNoteKey;

    //Select special MakerNote Tag for lens depending on Maker
    if ((CamMaker.find("pentax"))!=string::npos) {
        MakerNoteKey = "Exif.Pentax.LensType";
    }
    if ((CamMaker.find("canon"))!=string::npos) {
        MakerNoteKey = "Exif.CanonCs.LensType";
    }
    if ((CamMaker.find("minolta"))!=string::npos) {
        MakerNoteKey = "Exif.Minolta.LensID";
    }
    if ((CamMaker.find("nikon"))!=string::npos) {
        MakerNoteKey = "Exif.NikonLd3.LensIDNumber";
        if (exifData[MakerNoteKey].toString().size()==0) {
            MakerNoteKey = "Exif.NikonLd2.LensIDNumber";
        }
        if (exifData[MakerNoteKey].toString().size()==0) {
            MakerNoteKey = "Exif.NikonLd1.LensIDNumber";
        }
    }
    if ((CamMaker.find("olympus"))!=string::npos) {
        MakerNoteKey = "Exif.OlympusEq.LensType";
    }
    if ((CamMaker.find("sony"))!=string::npos) {
        //MakerNoteKey = "Exif.Sony1.LensID";
    }

    //Decode Lens ID
    if ((MakerNoteKey.size()>0) && (exifData[MakerNoteKey].toString().size()>0))  {
        Exiv2::ExifKey ek(MakerNoteKey);
        Exiv2::ExifData::const_iterator md = exifData.findKey(ek);
        if (md != exifData.end()) {
            LensNameMN = md->print(&exifData);

            //Modify some lens names for better searching in lfDatabase
            if ((CamMaker.find("nikon"))!=std::string::npos) {
                StrReplace(LensNameMN, "Nikon", "");
                StrReplace(LensNameMN, "Zoom-Nikkor", "");
            }
        }
    }

    if (camera) {
        if (LensNameMN.size()>8) {  // only take lens names with significant length
            lenses = ldb->FindLenses (camera, NULL, LensNameMN.c_str());
        } else {
            lenses = ldb->FindLenses (camera, NULL, NULL);
        }
        if (lenses) {
            lens = lenses[0];
            sLensfunParameters.Lens = string(lf_mlstr_get(lens->Model));
        }
        lf_free (lenses);
    }
    lf_free (cameras);

    sLensfunParameters.Focal = exifData["Exif.Photo.FocalLength"].toFloat();
    sLensfunParameters.Aperture = exifData["Exif.Photo.FNumber"].toFloat();

    if (DEBUG) {
        g_print("\nExif Data:\n");
        g_print("\tCamera: %s, %s\n", sLensfunParameters.CamMaker.c_str(), sLensfunParameters.Camera.c_str());
        g_print("\tLens: %s\n", sLensfunParameters.Lens.c_str());
        g_print("\tFocal Length: %f\n", sLensfunParameters.Focal);
        g_print("\tF-Stop: %f\n", sLensfunParameters.Aperture);
        g_print("\tCrop Factor: %f\n", sLensfunParameters.Crop);
        g_print("\tScale: %f\n", sLensfunParameters.Scale);
    }

    return 0;
}
//--------------------------------------------------------------------


//####################################################################
// store and load parameters and settings to/from gimp_data_storage
static void loadSettings() {

    gimp_get_data ("plug-in-gimplensfun", &sLensfunParameterStorage);

    sLensfunParameters.ModifyFlags = sLensfunParameterStorage.ModifyFlags;
    sLensfunParameters.Inverse  = sLensfunParameterStorage.Inverse;
    if (strlen(sLensfunParameterStorage.Camera)>0)
        sLensfunParameters.Camera  = std::string(sLensfunParameterStorage.Camera);
    if (strlen(sLensfunParameterStorage.CamMaker)>0)
        sLensfunParameters.CamMaker  = std::string(sLensfunParameterStorage.CamMaker);
    if (strlen(sLensfunParameterStorage.Lens)>0)
        sLensfunParameters.Lens  = std::string(sLensfunParameterStorage.Lens);
    sLensfunParameters.Scale    = sLensfunParameterStorage.Scale;
    sLensfunParameters.Crop     = sLensfunParameterStorage.Crop;
    sLensfunParameters.Focal    = sLensfunParameterStorage.Focal;
    sLensfunParameters.Aperture = sLensfunParameterStorage.Aperture;
    sLensfunParameters.Distance = sLensfunParameterStorage.Distance;
    sLensfunParameters.TargetGeom = sLensfunParameterStorage.TargetGeom;
}
//--------------------------------------------------------------------

static void storeSettings() {

    sLensfunParameterStorage.ModifyFlags = sLensfunParameters.ModifyFlags;
    sLensfunParameterStorage.Inverse  = sLensfunParameters.Inverse;
    strcpy( sLensfunParameterStorage.Camera, sLensfunParameters.Camera.c_str() );
    strcpy( sLensfunParameterStorage.CamMaker, sLensfunParameters.CamMaker.c_str() );
    strcpy( sLensfunParameterStorage.Lens, sLensfunParameters.Lens.c_str() );
    sLensfunParameterStorage.Scale    = sLensfunParameters.Scale;
    sLensfunParameterStorage.Crop     = sLensfunParameters.Crop;
    sLensfunParameterStorage.Focal    = sLensfunParameters.Focal;
    sLensfunParameterStorage.Aperture = sLensfunParameters.Aperture;
    sLensfunParameterStorage.Distance = sLensfunParameters.Distance;
    sLensfunParameterStorage.TargetGeom = sLensfunParameters.TargetGeom;

    gimp_set_data ("plug-in-gimplensfun", &sLensfunParameterStorage, sizeof (sLensfunParameterStorage));
}
//--------------------------------------------------------------------


//####################################################################
// Run()
static void
run (const gchar*   name,
     gint           nparams,
     const GimpParam*   param,
     gint*          nreturn_vals,
     GimpParam**    return_vals)
{
    static GimpParam    values[1];
    GimpPDBStatusType   status = GIMP_PDB_SUCCESS;
    gint32              imageID;
    GimpRunMode         run_mode;
    GimpDrawable        *drawable;

    /* Setting mandatory output values */
    *nreturn_vals = 1;
    *return_vals  = values;

    values[0].type = GIMP_PDB_STATUS;
    values[0].data.d_status = status;

    drawable = gimp_drawable_get (param[2].data.d_drawable);

    gimp_progress_init ("Lensfun correction...");

    imageID = param[1].data.d_drawable;

    if (DEBUG) g_print ("Loading database...");
    //Load lensfun database
    ldb = lf_db_new ();
    ldb->Load ();

    // read exif data
    const gchar *filename = gimp_image_get_filename(imageID);
    if (DEBUG) g_print ("Image file path: %s\n", filename);

    if ((filename == NULL) || (read_opts_from_exif(filename) != 0)) {
	    loadSettings();
    }

    run_mode = GimpRunMode(param[0].data.d_int32);
    if (run_mode == GIMP_RUN_INTERACTIVE)
    {
	    if (DEBUG) g_print ("Creating dialog...\n");
	    /* Display the dialog */
	    if (create_dialog_window (drawable)) {
		    process_image(drawable);
	    }
    } 
    else 
    {
	    /* If run_mode is non-interactive, we use the configuration
	     * from read_opts_from_exif. If that fails, we use the stored settings
	     * (loadSettings()), e.g. the settings that have been made in the last 
	     * interactive use of the plugin. One day, all settings should be 
	     * available as arguments in non-interactive mode.
	     */
	    process_image(drawable);
    }

    storeSettings();

    lf_db_destroy(ldb);

}
