#include <gtk/gtk.h>
#include <glib.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

// --- Data Structures ---
typedef struct {
    GtkWidget *window;
    GtkWidget *listbox;
    GtkWidget *textview;
    GtkWidget *pathLabel;
    GtkWidget *entryName;
    GtkWidget *statusLabel; // For metadata/status feedback
    gchar *current_dir;
    gchar *selected_file_path; // Full path of the currently selected file/dir
} AppWidgets;

// --- Function Prototypes ---
static void refresh_file_list(AppWidgets *w);
static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
static void on_new_clicked(GtkButton *btn, gpointer user_data);
static void on_save_clicked(GtkButton *btn, gpointer user_data);
static void on_delete_clicked(GtkButton *btn, gpointer user_data);
static void on_rename_clicked(GtkButton *btn, gpointer user_data);
static void on_up_clicked(GtkButton *btn, gpointer user_data);
static void show_error_dialog(GtkWindow *parent, const gchar *title, const gchar *message);
static void show_info_dialog(GtkWindow *parent, const gchar *title, const gchar *message);
static const gchar* get_row_name(GtkListBoxRow *row);

// --- Helper Functions ---

static const gchar* get_row_name(GtkListBoxRow *row)
{
    GtkWidget *hbox = gtk_bin_get_child(GTK_BIN(row));
    GList *hchildren = gtk_container_get_children(GTK_CONTAINER(hbox));
    if (!hchildren) return NULL;
    GtkWidget *label = GTK_WIDGET(hchildren->next->data);
    const gchar *text = gtk_label_get_text(GTK_LABEL(label));
    g_list_free_full(hchildren, NULL);
    return text;
}

static void show_error_dialog(GtkWindow *parent, const gchar *title, const gchar *message)
{
    GtkWidget *d = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s", message);
    gtk_window_set_title(GTK_WINDOW(d), title);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static void show_info_dialog(GtkWindow *parent, const gchar *title, const gchar *message)
{
    GtkWidget *d = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "%s", message);
    gtk_window_set_title(GTK_WINDOW(d), title);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

// --- File System Operations ---

static void refresh_file_list(AppWidgets *w)
{
    // Clear existing list
    GList *children = gtk_container_get_children(GTK_CONTAINER(w->listbox));
    for (GList *iter = children; iter != NULL; iter = iter->next) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
    
    // Clear editor and status on refresh
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->textview));
    gtk_text_buffer_set_text(buf, "", -1);
    gtk_label_set_text(GTK_LABEL(w->statusLabel), "Current File: None Selected");
    
    if (w->selected_file_path) {
        g_free(w->selected_file_path);
        w->selected_file_path = NULL;
    }

    // Update path label
    gtk_label_set_text(GTK_LABEL(w->pathLabel), w->current_dir);

    GDir *dir = g_dir_open(w->current_dir, 0, NULL);
    if (!dir) {
        show_error_dialog(GTK_WINDOW(w->window), "Navigation Error", "Cannot open directory: Permission denied or not found.");
        return;
    }

    const gchar *name;
    GPtrArray *entries = g_ptr_array_new_with_free_func(g_free);

    while ((name = g_dir_read_name(dir)) != NULL) {
        if (g_strcmp0(name, ".") == 0 || g_strcmp0(name, "..") == 0) continue;
        g_ptr_array_add(entries, g_strdup(name));
    }
    g_dir_close(dir);

    g_ptr_array_sort(entries, (GCompareFunc)g_ascii_strcasecmp);

    for (guint i = 0; i < entries->len; ++i) {
        gchar *entryName = g_ptr_array_index(entries, i);
        gchar *fullpath = g_build_filename(w->current_dir, entryName, NULL);

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

        GtkWidget *image;
        if (g_file_test(fullpath, G_FILE_TEST_IS_DIR))
            image = gtk_image_new_from_icon_name("folder", GTK_ICON_SIZE_SMALL_TOOLBAR);
        else
            image = gtk_image_new_from_icon_name("text-x-generic", GTK_ICON_SIZE_SMALL_TOOLBAR);

        GtkWidget *label = gtk_label_new(entryName);
        gtk_widget_set_halign(label, GTK_ALIGN_START);

        gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 6);
        gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 6);
        gtk_container_add(GTK_CONTAINER(row), hbox);
        gtk_widget_show_all(row);

        g_object_set_data_full(G_OBJECT(row), "entry-name", g_strdup(entryName), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(w->listbox), row, -1);

        g_free(fullpath);
    }

    g_ptr_array_free(entries, TRUE);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    const gchar *entryName = g_object_get_data(G_OBJECT(row), "entry-name");
    
    if (w->selected_file_path) {
        g_free(w->selected_file_path);
        w->selected_file_path = NULL;
    }
    w->selected_file_path = g_build_filename(w->current_dir, entryName, NULL);

    if (g_file_test(w->selected_file_path, G_FILE_TEST_IS_DIR)) {
        // Navigate into directory
        g_free(w->current_dir);
        w->current_dir = g_strdup(w->selected_file_path);
        refresh_file_list(w);
        gtk_label_set_text(GTK_LABEL(w->statusLabel), "Current File: None Selected (Directory View)");
        
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->textview));
        gtk_text_buffer_set_text(buf, "", -1);

    } else {
        // Read file contents (READ) and display metadata (Optional Feature)
        gchar *contents = NULL;
        gsize len = 0;
        GError *err = NULL;

        if (g_file_get_contents(w->selected_file_path, &contents, &len, &err)) {
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->textview));
            gtk_text_buffer_set_text(buf, contents, len);
            g_free(contents);
            
            // Display metadata (size and time)
            struct stat file_stat;
            if (stat(w->selected_file_path, &file_stat) == 0) {
                gchar *time_str = g_time_val_to_iso8601(&file_stat.st_mtim, NULL);
                gchar *status_text = g_strdup_printf("Current File: %s | Size: %lu bytes | Modified: %s", 
                                                    entryName, 
                                                    (gulong)file_stat.st_size, 
                                                    time_str);
                gtk_label_set_text(GTK_LABEL(w->statusLabel), status_text);
                g_free(time_str);
                g_free(status_text);
            } else {
                gtk_label_set_text(GTK_LABEL(w->statusLabel), "Current File: Metadata unavailable");
            }

        } else {
            show_error_dialog(GTK_WINDOW(w->window), "File Read Error", err->message);
            g_error_free(err);
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->textview));
            gtk_text_buffer_set_text(buf, "", -1);
            gtk_label_set_text(GTK_LABEL(w->statusLabel), "Current File: Read Failed");
        }
    }
    
    // Clear selection path (it's re-set on activation)
    if (w->selected_file_path) {
        g_free(w->selected_file_path);
        w->selected_file_path = NULL;
    }
}

static void on_new_clicked(GtkButton *btn, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    const gchar *name = gtk_entry_get_text(GTK_ENTRY(w->entryName));
    if (!name || !*name) {
        show_error_dialog(GTK_WINDOW(w->window), "Create Error", "Please enter a name.");
        return;
    }

    gchar *fullpath = g_build_filename(w->current_dir, name, NULL);
    GError *err = NULL;
    gboolean is_dir = (g_str_has_suffix(name, "/") || g_str_has_suffix(name, "\\"));
    
    if (g_file_test(fullpath, G_FILE_TEST_EXISTS)) {
        show_error_dialog(GTK_WINDOW(w->window), "Create Error", "File or directory already exists.");
        g_free(fullpath);
        return;
    }

    if (is_dir) {
        // CREATE Directory
        if (g_mkdir_with_parents(fullpath, 0755) != 0) {
            show_error_dialog(GTK_WINDOW(w->window), "Create Directory Error", g_strerror(errno));
            g_free(fullpath);
            return;
        }
    } else {
        // CREATE File
        if (!g_file_set_contents(fullpath, "", 0, &err)) {
            show_error_dialog(GTK_WINDOW(w->window), "Create File Error", err->message);
            g_error_free(err);
            g_free(fullpath);
            return;
        }
    }

    refresh_file_list(w);
    show_info_dialog(GTK_WINDOW(w->window), "Success", is_dir ? "Directory created." : "File created.");
    gtk_entry_set_text(GTK_ENTRY(w->entryName), "");
    g_free(fullpath);
}

static void on_save_clicked(GtkButton *btn, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(w->listbox));
    if (!row) {
        show_error_dialog(GTK_WINDOW(w->window), "Save Error", "Please select a file to save.");
        return;
    }

    const gchar *name = g_object_get_data(G_OBJECT(row), "entry-name");
    gchar *fullpath = g_build_filename(w->current_dir, name, NULL);
    
    if (g_file_test(fullpath, G_FILE_TEST_IS_DIR)) {
        show_error_dialog(GTK_WINDOW(w->window), "Save Error", "Cannot save content to a directory.");
        g_free(fullpath);
        return;
    }

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->textview));
    GtkTextIter s, e;
    gtk_text_buffer_get_start_iter(buf, &s);
    gtk_text_buffer_get_end_iter(buf, &e);

    gchar *text = gtk_text_buffer_get_text(buf, &s, &e, FALSE);
    GError *err = NULL;

    // UPDATE operation
    if (!g_file_set_contents(fullpath, text, -1, &err)) {
        show_error_dialog(GTK_WINDOW(w->window), "Save Error", err->message);
        g_error_free(err);
        g_free(text);
        g_free(fullpath);
        return;
    }

    g_free(text);
    show_info_dialog(GTK_WINDOW(w->window), "Success", "File saved (updated).");
    
    // Trigger row activation to refresh metadata display
    on_row_activated(GTK_LIST_BOX(w->listbox), row, w);
    g_free(fullpath);
}

static void on_delete_clicked(GtkButton *btn, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(w->listbox));
    if (!row) {
        show_error_dialog(GTK_WINDOW(w->window), "Delete Error", "Please select an item.");
        return;
    }

    const gchar *name = g_object_get_data(G_OBJECT(row), "entry-name");
    gchar *fullpath = g_build_filename(w->current_dir, name, NULL);

    GtkWidget *c = gtk_message_dialog_new(GTK_WINDOW(w->window),
                                          GTK_DIALOG_MODAL,
                                          GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_YES_NO,
                                          "Confirm deletion of \"%s\"? This cannot be undone.", name);
    gint res = gtk_dialog_run(GTK_DIALOG(c));
    gtk_widget_destroy(c);

    if (res != GTK_RESPONSE_YES) {
        g_free(fullpath);
        return;
    }

    GFile *file_to_delete = g_file_new_for_path(fullpath);
    GError *err = NULL;
    
    // DELETE operation: Use GFile for recursive deletion (non-empty directories)
    if (!g_file_delete(file_to_delete, G_FILE_COPY_RECURSIVE, NULL, &err)) {
        show_error_dialog(GTK_WINDOW(w->window), "Delete Error", err->message);
        g_error_free(err);
        g_object_unref(file_to_delete);
        g_free(fullpath);
        return;
    }

    g_object_unref(file_to_delete);
    refresh_file_list(w);
    show_info_dialog(GTK_WINDOW(w->window), "Success", "Item deleted.");
    g_free(fullpath);
}

static void on_rename_clicked(GtkButton *btn, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    const gchar *newName = gtk_entry_get_text(GTK_ENTRY(w->entryName));
    if (!newName || !*newName) {
        show_error_dialog(GTK_WINDOW(w->window), "Rename Error", "Please enter new name.");
        return;
    }

    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(w->listbox));
    if (!row) {
        show_error_dialog(GTK_WINDOW(w->window), "Rename Error", "Please select an item.");
        return;
    }

    const gchar *oldName = g_object_get_data(G_OBJECT(row), "entry-name");
    gchar *oldpath = g_build_filename(w->current_dir, oldName, NULL);
    gchar *newpath = g_build_filename(w->current_dir, newName, NULL);

    if (g_file_test(newpath, G_FILE_TEST_EXISTS)) {
        show_error_dialog(GTK_WINDOW(w->window), "Rename Error", "Item with the new name already exists.");
        g_free(oldpath);
        g_free(newpath);
        return;
    }

    // RENAME operation
    if (rename(oldpath, newpath) != 0) {
        gchar *err_msg = g_strdup_printf("Rename failed: %s", g_strerror(errno));
        show_error_dialog(GTK_WINDOW(w->window), "Rename Error", err_msg);
        g_free(err_msg);
        g_free(oldpath);
        g_free(newpath);
        return;
    }

    refresh_file_list(w);
    show_info_dialog(GTK_WINDOW(w->window), "Success", "Item renamed.");
    gtk_entry_set_text(GTK_ENTRY(w->entryName), "");
    g_free(oldpath);
    g_free(newpath);
}

static void on_up_clicked(GtkButton *btn, gpointer user_data)
{
    AppWidgets *w = (AppWidgets *)user_data;
    gchar *parent = g_path_get_dirname(w->current_dir);

    // Stop if we are at the root (parent == current_dir)
    if (g_strcmp0(parent, w->current_dir) == 0) {
        g_free(parent);
        return;
    }

    g_free(w->current_dir);
    w->current_dir = g_strdup(parent);
    g_free(parent);

    // NAVIGATE operation (up)
    refresh_file_list(w);
}

// --- Main Application Setup ---

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    AppWidgets *w = g_new0(AppWidgets, 1);

    gchar *cwd = g_get_current_dir();
    w->current_dir = g_strdup(cwd);
    g_free(cwd);

    w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w->window), "CS 3502 File Manager - OwlTech FS Division");
    gtk_window_set_default_size(GTK_WINDOW(w->window), 900, 600);
    g_signal_connect(w->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(w->window), vbox);
    
    // 1. Current Path Display / Up Button
    GtkWidget *path_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    w->pathLabel = gtk_label_new(w->current_dir);
    gtk_label_set_xalign(GTK_LABEL(w->pathLabel), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(w->pathLabel), PANGO_ELLIPSIZE_START);
    GtkWidget *up_button = gtk_button_new_with_label("Go Up");
    gtk_box_pack_start(GTK_BOX(path_hbox), w->pathLabel, TRUE, TRUE, 6);
    gtk_box_pack_end(GTK_BOX(path_hbox), up_button, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(vbox), path_hbox, FALSE, FALSE, 6);
    
    // Main Paned Window (Listbox | Editor)
    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(hpaned, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), hpaned, TRUE, TRUE, 0);

    // --- Left Pane (File/Directory Display) ---
    GtkWidget *left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(left_vbox, 320, -1);
    gtk_paned_pack1(GTK_PANED(hpaned), left_vbox, FALSE, TRUE);

    GtkWidget *scrolled_list = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_list), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_list, TRUE);
    gtk_box_pack_start(GTK_BOX(left_vbox), scrolled_list, TRUE, TRUE, 0);

    w->listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(w->listbox), GTK_SELECTION_SINGLE);
    gtk_container_add(GTK_CONTAINER(scrolled_list), w->listbox);

    // Entry field for New/Rename operations
    w->entryName = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->entryName), "Name (File or Folder ending with /)");
    gtk_box_pack_start(GTK_BOX(left_vbox), w->entryName, FALSE, FALSE, 6);

    // Action Buttons
    GtkWidget *btns_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *new_button = gtk_button_new_with_label("New"); // CREATE
    GtkWidget *rename_button = gtk_button_new_with_label("Rename"); // RENAME
    GtkWidget *delete_button = gtk_button_new_with_label("Delete"); // DELETE
    gtk_box_pack_start(GTK_BOX(btns_hbox), new_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btns_hbox), rename_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btns_hbox), delete_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left_vbox), btns_hbox, FALSE, FALSE, 6);


    // --- Right Pane (File Content Area) ---
    GtkWidget *right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_paned_pack2(GTK_PANED(hpaned), right_vbox, TRUE, TRUE);

    GtkWidget *editor_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(editor_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(editor_scrolled, TRUE);
    gtk_box_pack_start(GTK_BOX(right_vbox), editor_scrolled, TRUE, TRUE, 0);

    w->textview = gtk_text_view_new(); // READ/UPDATE
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w->textview), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(editor_scrolled), w->textview);
    
    // Save button
    GtkWidget *save_button = gtk_button_new_with_label("Save Changes");
    gtk_box_pack_end(GTK_BOX(right_vbox), save_button, FALSE, FALSE, 6);
    
    // Status/Feedback Area
    w->statusLabel = gtk_label_new("Current File: None Selected");
    gtk_label_set_xalign(GTK_LABEL(w->statusLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(right_vbox), w->statusLabel, FALSE, FALSE, 4);

    // --- Connect Signals ---
    g_signal_connect(w->listbox, "row-activated", G_CALLBACK(on_row_activated), w);
    g_signal_connect(new_button, "clicked", G_CALLBACK(on_new_clicked), w);
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), w);
    g_signal_connect(delete_button, "clicked", G_CALLBACK(on_delete_clicked), w);
    g_signal_connect(rename_button, "clicked", G_CALLBACK(on_rename_clicked), w);
    g_signal_connect(up_button, "clicked", G_CALLBACK(on_up_clicked), w);

    refresh_file_list(w);
    gtk_widget_show_all(w->window);
    gtk_main();

    g_free(w->current_dir);
    g_free(w);
    return 0;
}
