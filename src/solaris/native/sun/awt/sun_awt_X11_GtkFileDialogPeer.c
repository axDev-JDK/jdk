#include <jni.h>
#include <stdio.h>
#include <jni_util.h>
#include <string.h>
#include "gtk2_interface.h"
#include "sun_awt_X11_GtkFileDialogPeer.h"
#include "java_awt_FileDialog.h"
#include "debug_assert.h"

static JavaVM *jvm;

/* To cache some method IDs */
static jmethodID filenameFilterCallbackMethodID = NULL;
static jmethodID setFileInternalMethodID = NULL;
static jfieldID  widgetFieldID = NULL;

JNIEXPORT void JNICALL Java_sun_awt_X11_GtkFileDialogPeer_initIDs
(JNIEnv *env, jclass cx)
{
    filenameFilterCallbackMethodID = (*env)->GetMethodID(env, cx,
            "filenameFilterCallback", "(Ljava/lang/String;)Z");
    DASSERT(filenameFilterCallbackMethodID != NULL);

    setFileInternalMethodID = (*env)->GetMethodID(env, cx,
            "setFileInternal", "(Ljava/lang/String;[Ljava/lang/String;)V");
    DASSERT(setFileInternalMethodID != NULL);

    widgetFieldID = (*env)->GetFieldID(env, cx, "widget", "J");
    DASSERT(widgetFieldID != NULL);
}

static gboolean filenameFilterCallback(const GtkFileFilterInfo * filter_info, gpointer obj)
{
    JNIEnv *env;
    jclass cx;
    jstring filename;

    env = (JNIEnv *) JNU_GetEnv(jvm, JNI_VERSION_1_2);

    filename = (*env)->NewStringUTF(env, filter_info->filename);

    return (*env)->CallBooleanMethod(env, obj, filenameFilterCallbackMethodID,
            filename);
}

static void quit(JNIEnv * env, jobject jpeer, gboolean isSignalHandler)
{
    GtkWidget * dialog = (GtkWidget*)jlong_to_ptr(
            (*env)->GetLongField(env, jpeer, widgetFieldID));

    if (dialog != NULL)
    {
        // Callbacks from GTK signals are made within the GTK lock
        // So, within a signal handler there is no need to call
        // gdk_threads_enter() / gdk_threads_leave()
        if (!isSignalHandler) {
            gdk_threads_enter();
        }

        gtk_widget_hide (dialog);
        gtk_widget_destroy (dialog);

        gtk_main_quit ();

        (*env)->SetLongField(env, jpeer, widgetFieldID, 0);

        if (!isSignalHandler) {
            gdk_threads_leave();
        }
    }
}

/*
 * Class:     sun_awt_X11_GtkFileDialogPeer
 * Method:    quit
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_sun_awt_X11_GtkFileDialogPeer_quit
(JNIEnv * env, jobject jpeer)
{
    quit(env, jpeer, FALSE);
}

/*
 * Class:     sun_awt_X11_GtkFileDialogPeer
 * Method:    toFront
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_sun_awt_X11_GtkFileDialogPeer_toFront
(JNIEnv * env, jobject jpeer)
{
    GtkWidget * dialog;

    gdk_threads_enter();

    dialog = (GtkWidget*)jlong_to_ptr(
            (*env)->GetLongField(env, jpeer, widgetFieldID));

    if (dialog != NULL) {
        gtk_window_present((GtkWindow*)dialog);
    }

    gdk_threads_leave();
}

/*
 * Class:     sun_awt_X11_GtkFileDialogPeer
 * Method:    setBounds
 * Signature: (IIIII)V
 */
JNIEXPORT void JNICALL Java_sun_awt_X11_GtkFileDialogPeer_setBounds
(JNIEnv * env, jobject jpeer, jint x, jint y, jint width, jint height, jint op)
{
    GtkWindow* dialog;

    gdk_threads_enter();

    dialog = (GtkWindow*)jlong_to_ptr(
        (*env)->GetLongField(env, jpeer, widgetFieldID));

    if (dialog != NULL) {
        if (x >= 0 && y >= 0) {
            gtk_window_move(dialog, (gint)x, (gint)y);
        }
        if (width > 0 && height > 0) {
            gtk_window_resize(dialog, (gint)width, (gint)height);
        }
    }

    gdk_threads_leave();
}

/**
 * Convert a GSList to an array of filenames (without the parent folder)
 */
static jobjectArray toFilenamesArray(JNIEnv *env, GSList* list)
{
    jstring str;
    jclass stringCls;
    GSList *iterator;
    jobjectArray array;
    int i;
    char* entry;

    if (NULL == list) {
        return NULL;
    }

    stringCls = (*env)->FindClass(env, "java/lang/String");
    if (stringCls == NULL) {
        JNU_ThrowInternalError(env, "Could not get java.lang.String class");
        return NULL;
    }

    array = (*env)->NewObjectArray(env, g_slist_length(list), stringCls,
            NULL);
    if (array == NULL) {
        JNU_ThrowInternalError(env, "Could not instantiate array files array");
        return NULL;
    }

    i = 0;
    for (iterator = list; iterator; iterator = iterator->next) {
        entry = (char*) iterator->data;
        entry = strrchr(entry, '/') + 1;
        str = (*env)->NewStringUTF(env, entry);
        (*env)->SetObjectArrayElement(env, array, i, str);
        i++;
    }

    return array;
}

static void handle_response(GtkWidget* aDialog, gint responseId, gpointer obj)
{
    JNIEnv *env;
    char *current_folder;
    GSList *filenames;
    jclass cx;
    jstring jcurrent_folder;
    jobjectArray jfilenames;

    env = (JNIEnv *) JNU_GetEnv(jvm, JNI_VERSION_1_2);
    current_folder = NULL;
    filenames = NULL;

    if (responseId == GTK_RESPONSE_ACCEPT) {
        current_folder = gtk_file_chooser_get_current_folder(
                GTK_FILE_CHOOSER(aDialog));
        filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(aDialog));
    }

    jcurrent_folder = (*env)->NewStringUTF(env, current_folder);
    jfilenames = toFilenamesArray(env, filenames);

    (*env)->CallVoidMethod(env, obj, setFileInternalMethodID, jcurrent_folder,
            jfilenames);
    g_free(current_folder);

    quit(env, (jobject)obj, TRUE);
}

/*
 * Class:     sun_awt_X11_GtkFileDialogPeer
 * Method:    run
 * Signature: (Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/io/FilenameFilter;ZII)V
 */
JNIEXPORT void JNICALL
Java_sun_awt_X11_GtkFileDialogPeer_run(JNIEnv * env, jobject jpeer,
        jstring jtitle, jint mode, jstring jdir, jstring jfile,
        jobject jfilter, jboolean multiple, int x, int y)
{
    GtkWidget *dialog = NULL;
    GtkFileFilter *filter;

    if (jvm == NULL) {
        (*env)->GetJavaVM(env, &jvm);
    }

    gdk_threads_enter();

    const char *title = jtitle == NULL? "": (*env)->GetStringUTFChars(env, jtitle, 0);

    if (mode == java_awt_FileDialog_SAVE) {
        /* Save action */
        dialog = gtk_file_chooser_dialog_new(title, NULL,
                GTK_FILE_CHOOSER_ACTION_SAVE, GTK_STOCK_CANCEL,
                GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
    }
    else {
        /* Default action OPEN */
        dialog = gtk_file_chooser_dialog_new(title, NULL,
                GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL,
                GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);

        /* Set multiple selection mode, that is allowed only in OPEN action */
        if (multiple) {
            gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog),
                    multiple);
        }
    }

    if (jtitle != NULL) {
      (*env)->ReleaseStringUTFChars(env, jtitle, title);
    }

    /* Set the directory */
    if (jdir != NULL) {
        const char *dir = (*env)->GetStringUTFChars(env, jdir, 0);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), dir);
        (*env)->ReleaseStringUTFChars(env, jdir, dir);
    }

    /* Set the filename */
    if (jfile != NULL) {
        const char *filename = (*env)->GetStringUTFChars(env, jfile, 0);
        if (mode == java_awt_FileDialog_SAVE) {
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), filename);
        } else {
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), filename);
        }
        (*env)->ReleaseStringUTFChars(env, jfile, filename);
    }

    /* Set the file filter */
    if (jfilter != NULL) {
        filter = gtk_file_filter_new();
        gtk_file_filter_add_custom(filter, GTK_FILE_FILTER_FILENAME,
                filenameFilterCallback, jpeer, NULL);
        gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filter);
    }

    /* Other Properties */
    if (gtk_check_version(2, 8, 0) == NULL) {
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(
                dialog), TRUE);
    }

    /* Set the initial location */
    if (x >= 0 && y >= 0) {
        gtk_window_move((GtkWindow*)dialog, (gint)x, (gint)y);

        // NOTE: it doesn't set the initial size for the file chooser
        // as it seems like the file chooser overrides the size internally
    }

    g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(
            handle_response), jpeer);

    (*env)->SetLongField(env, jpeer, widgetFieldID, ptr_to_jlong(dialog));

    gtk_widget_show(dialog);

    gtk_main();
    gdk_threads_leave();
}

