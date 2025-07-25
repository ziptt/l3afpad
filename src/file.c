/*
 *  L3afpad - GTK+ based simple text editor
 *  Copyright (C) 2004-2005 Tarot Osuji
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <glib/gprintf.h>
#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include "l3afpad.h"

#define SALT_SIZE crypto_pwhash_SALTBYTES
#define NONCE_SIZE crypto_secretbox_NONCEBYTES
#define KEY_SIZE crypto_secretbox_KEYBYTES

void die(const char *msg) {
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

gboolean check_file_writable(gchar *filename)
{
	FILE *fp;

	if ((fp = fopen(filename, "a")) != NULL) {
		fclose(fp);
		return TRUE;
	}
	return FALSE;
}

gchar *get_file_basename(gchar *filename, gboolean bracket)
{
	gchar *basename = NULL;
	gchar *tmp;
	gboolean exist_flag;

	if (filename) {
		tmp = g_path_get_basename(
			g_filename_to_utf8(filename, -1, NULL, NULL, NULL));
		exist_flag = g_file_test(
			g_filename_to_utf8(filename, -1, NULL, NULL, NULL),
			G_FILE_TEST_EXISTS);
	} else {
		tmp = g_strdup(_("Untitled"));
		exist_flag = FALSE;
	}

	if (bracket) {
		if (!exist_flag) {
			GString *string = g_string_new(tmp);
			g_string_prepend(string, "(");
			g_string_append(string, ")");
			basename = g_strdup(string->str);
			g_string_free(string, TRUE);
		} else if (!check_file_writable(filename)) {
			GString *string = g_string_new(tmp);
			g_string_prepend(string, "<");
			g_string_append(string, ">");
			basename = g_strdup(string->str);
			g_string_free(string, TRUE);
		}
	}

	if (!basename)
		basename = g_strdup(tmp);
	g_free(tmp);

	return basename;
}

gchar *parse_file_uri(gchar *uri)
{
	gchar *filename;
//	gchar **strs;

	if (g_strstr_len(uri, 5, "file:"))
		filename = g_filename_from_uri(uri, NULL, NULL);
	else {
		if (g_path_is_absolute(uri))
			filename = g_strdup(uri);
		else
			filename = g_build_filename(g_get_current_dir(), uri, NULL);
	}
/*	if (strstr(filename, " ")) {
		strs = g_strsplit(filename, " ", -1);
		g_free(filename);
		filename = g_strjoinv("\\ ", strs);
		g_strfreev(strs);
	}
*/
	return filename;
}

gint file_open_real(GtkWidget *view, FileInfo *fi)
{
	gchar *contents;
	gsize length;
	GError *err = NULL;
	const gchar *charset;
	gchar *str = NULL;
	GtkTextIter iter;

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

	if (!g_file_get_contents(fi->filename, &contents, &length, &err)) {
		if (g_file_test(fi->filename, G_FILE_TEST_EXISTS)) {
			run_dialog_message(gtk_widget_get_toplevel(view),
				GTK_MESSAGE_ERROR, err->message);
			g_error_free(err);
			return -1;
		}
		g_error_free(err);
		err = NULL;
		contents = g_strdup("");
	}

	guchar salt[SALT_SIZE];
	guchar nonce[NONCE_SIZE];

	memcpy(salt, contents, SALT_SIZE);
	memcpy(nonce, contents + SALT_SIZE, NONCE_SIZE);

	//gchar *password = "password";

	//GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gchar *password = get_user_input();

	if (password == NULL || strcmp(password, "") == 0) goto skipdec;

	guchar key[KEY_SIZE];
	if (crypto_pwhash(key, sizeof key, password, strlen(password), salt,
					crypto_pwhash_OPSLIMIT_INTERACTIVE,
					crypto_pwhash_MEMLIMIT_INTERACTIVE,
					crypto_pwhash_ALG_DEFAULT) != 0) {
		die("Password hashing failed");
	}

	guchar *ciphertext = (guchar *)contents + SALT_SIZE + NONCE_SIZE;
	gsize ciphertext_len = length - SALT_SIZE - NONCE_SIZE;

	guchar *decrypted = malloc(ciphertext_len - crypto_secretbox_MACBYTES);
	if (!decrypted) die("Cannot allocate memory");

	if (crypto_secretbox_open_easy(decrypted, ciphertext, ciphertext_len, nonce, key) != 0) {
		die("Decryption failed: wrong password or corrupted file");
	}

	skipdec:

	fi->lineend = detect_line_ending(decrypted);
	if (fi->lineend != LF)
		convert_line_ending_to_lf(decrypted);

	if (fi->charset)
		charset = fi->charset;
	else {
		charset = detect_charset(decrypted);
		if (charset == NULL)
			charset = get_default_charset();
	}

	if (length)
		do {
			if (err) {
				charset = "ISO-8859-1";
				g_error_free(err);
				err = NULL;
			}
			str = g_convert((gchar *)decrypted, -1, "UTF-8", charset, NULL, NULL, &err);
		} while (err);
	else
		str = g_strdup("");
	g_free(contents);

	if (charset != fi->charset) {
		g_free(fi->charset);
		fi->charset = g_strdup(charset);
		if (fi->charset_flag)
			fi->charset_flag = FALSE;
	}

	//g_free(decrypted);

//	undo_disconnect_signal(textbuffer);
//	undo_block_signal(buffer);
	force_block_cb_modified_changed(view);

	gtk_text_buffer_set_text(buffer, "", 0);
	gtk_text_buffer_get_start_iter(buffer, &iter);
	gtk_text_buffer_insert(buffer, &iter, str, strlen(str));
	gtk_text_buffer_get_start_iter(buffer, &iter);
	gtk_text_buffer_place_cursor(buffer, &iter);
	gtk_text_buffer_set_modified(buffer, FALSE);
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view), &iter, 0, FALSE, 0, 0);
	g_free(str);

	force_unblock_cb_modified_changed(view);
	menu_sensitivity_from_modified_flag(FALSE);
//	undo_unblock_signal(buffer);

	return 0;
}

gint file_save_real(GtkWidget *view, FileInfo *fi)
{
	FILE *fp;
	GtkTextIter start, end;
	gchar *str, *cstr;
	guchar *cstr_encrypted;
	gsize rbytes, wbytes;
	GError *err = NULL;

	guchar salt[SALT_SIZE];
	guchar nonce[NONCE_SIZE];
	guchar key[KEY_SIZE];

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

	gtk_text_buffer_get_start_iter(buffer, &start);
	gtk_text_buffer_get_end_iter(buffer, &end);
	str = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

	switch (fi->lineend) {
	case CR:
		convert_line_ending(&str, CR);
		break;
	case CR+LF:
		convert_line_ending(&str, CR+LF);
	}

	if (!fi->charset)
		fi->charset = g_strdup(get_default_charset());
	cstr = g_convert(str, -1, fi->charset, "UTF-8", &rbytes, &wbytes, &err);
	g_free(str);
	if (err) {
		switch (err->code) {
		case G_CONVERT_ERROR_ILLEGAL_SEQUENCE:
			run_dialog_message(gtk_widget_get_toplevel(view),
				GTK_MESSAGE_ERROR, _("Can't convert codeset to '%s'"), fi->charset);
			break;
		default:
			run_dialog_message(gtk_widget_get_toplevel(view),
				GTK_MESSAGE_ERROR, err->message);
		}
		g_error_free(err);
		return -1;
	}

	//gchar *password = "password";

	//GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gchar *password = get_user_input();

	if (password) {
		randombytes_buf(salt, sizeof salt);
		randombytes_buf(nonce, sizeof nonce);

		if (crypto_pwhash(key, sizeof key, password, strlen(password), salt,
			crypto_pwhash_OPSLIMIT_INTERACTIVE,
			crypto_pwhash_MEMLIMIT_INTERACTIVE,
			crypto_pwhash_ALG_DEFAULT) != 0) {
				die("Test Error");
		}

		size_t plaintext_len = strlen(cstr);
		size_t ciphertext_len = plaintext_len + crypto_secretbox_MACBYTES;
		size_t total_len = SALT_SIZE + NONCE_SIZE + ciphertext_len;

		guchar *output = malloc(total_len);

		if (crypto_secretbox_easy(output + SALT_SIZE + NONCE_SIZE, (guchar *)cstr, plaintext_len, nonce, key) != 0) {
			die("Encryption failed");
		}

		memcpy(output, salt, SALT_SIZE);
		memcpy(output + SALT_SIZE, nonce, NONCE_SIZE);

		//TODO sodium_memzero()

		cstr_encrypted = output;

		fp = fopen(fi->filename, "w");
		if (!fp) {
			run_dialog_message(gtk_widget_get_toplevel(view),
				GTK_MESSAGE_ERROR, _("Can't open file to write"));
			return -1;
		}
		if (fwrite(cstr_encrypted, 1, total_len, fp) != total_len) {
			run_dialog_message(gtk_widget_get_toplevel(view),
				GTK_MESSAGE_ERROR, _("Can't write file"));
			fclose(fp);
			return -1;
		}

		gtk_text_buffer_set_modified(buffer, FALSE);
		fclose(fp);
		g_free(cstr);
		g_free(cstr_encrypted);
	}

	return 0;
}

#if ENABLE_STATISTICS
void text_stats(gchar * text, gint * wc, gint * lc);
gint skipDelim(gchar ** pos);
gboolean isDelim(gchar);

gchar * file_stats(GtkWidget *view, FileInfo *fi)
{
	GtkTextIter start;
	GtkTextIter end;
	GtkTextIter textStart;
	GtkTextIter textEnd;
	gchar * str;
	gchar * text;
	gint totalLines = 0;
	gint totalChars = 0;
	gint totalWords = 0;
	gint charCount  = 0;
	gint wordCount  = 0;
	gint lineCount  = 0;
	gchar * toret = g_malloc( 8192 );
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	gboolean hasSelection = gtk_text_buffer_get_selection_bounds( buffer, &start, &end );

	if ( !hasSelection ) {
		gtk_text_buffer_get_start_iter(buffer, &start);
		gtk_text_buffer_get_start_iter(buffer, &end);
	}

	gtk_text_buffer_get_start_iter(buffer, &textStart);
	gtk_text_buffer_get_end_iter(buffer, &textEnd);

	str  = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
	text = gtk_text_buffer_get_text(buffer, &textStart, &textEnd, FALSE);

	totalChars = gtk_text_buffer_get_char_count( buffer );
	charCount  = strlen( str );

	text_stats( str, &wordCount, &lineCount );
	text_stats( text, &totalWords, &totalLines );

	g_sprintf(
		toret,
		_("<u>Totals count</u>\nChars: %7d Words: %6d Lines: %5d\n\n"
		"<u>Selection</u>\nChars: %7d Words: %6d Lines: %5d\n"),
		totalChars,
		totalWords,
		totalLines,
		charCount,
		wordCount,
		lineCount
	);

	return toret;
}

const gchar * DelimChars = " ,.;:\t\n-_?¿()!¡'/&%$#\"\\|{}[]+*";

void text_stats(gchar * text, gint * wc, gint * lc)
{
	gchar * pos = text;
	*wc = 0;
	*lc = 1;

	*lc += skipDelim( &pos );
	while( *pos != 0 ) {
		++(*wc);
		while( *pos != 0
		    && !isDelim( *pos ) )
		{
			++pos;
		}

		*lc += skipDelim( &pos );
	}
}

gint skipDelim(gchar ** pos)
{
	gint lc = 0;

	while( **pos != 0
            && isDelim( **pos ) )
	{
		if ( **pos == '\n' ) {
			++lc;
		}
		++( *pos );
	}

	return lc;
}

inline
gboolean isDelim(gchar ch)
{
	return ( strchr( DelimChars, ch ) != NULL );
}
#endif
