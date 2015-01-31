/* librepo - A library providing (libcURL like) API to downloading repository
 * Copyright (C) 2014  Tomas Mlcoch
 * Copyright (C) 2014  Richard Hughes
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <assert.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include "librepo.h"
#include "repoconf.h"
#include "repoconf_internal.h"
#include "cleanup.h"

static LrYumRepoFile *
lr_yum_repofile_init(const gchar *path, GKeyFile *keyfile)
{
    LrYumRepoFile *repofile = lr_malloc0(sizeof(*repofile));
    repofile->path = g_strdup(path);
    repofile->keyfile = keyfile;
    return repofile;
}

static void
lr_yum_repofile_free(LrYumRepoFile *repofile)
{
    if (!repofile)
        return;
    g_free(repofile->path);
    g_key_file_unref(repofile->keyfile);
    g_free(repofile);
}

static GKeyFile *
repofile_keyfile(LrYumRepoFile * repofile)
{
    return repofile->keyfile;
}

static LrYumRepoConf *
lr_yum_repoconf_init(LrYumRepoFile *repofile, const gchar *id)
{
    LrYumRepoConf *repoconf = lr_malloc0(sizeof(*repoconf));
    repoconf->file = repofile;
    repoconf->id = g_strdup(id);
    return repoconf;
}

static void
lr_yum_repoconf_free(LrYumRepoConf *repoconf)
{
    if (!repoconf)
        return;
    g_free(repoconf->id);
    g_free(repoconf);
}

static GKeyFile *
repoconf_keyfile(LrYumRepoConf *repoconf)
{
    return repofile_keyfile(repoconf->file);
}

gchar *
repoconf_id(LrYumRepoConf *repoconf)
{
    return repoconf->id;
}

LrYumRepoConfs *
lr_yum_repoconfs_init(void)
{
    LrYumRepoConfs *repos = lr_malloc0(sizeof(*repos));
    return repos;
}

void
lr_yum_repoconfs_free(LrYumRepoConfs *repos)
{
    g_slist_free_full(repos->repos, (GDestroyNotify) lr_yum_repoconf_free);
    g_free(repos);
}

GSList *
lr_yum_repoconfs_get_list(LrYumRepoConfs *repos, G_GNUC_UNUSED GError **err)
{
    return repos->repos;
}

/* This function is taken from libhif
 * Original author: Richard Hughes <richard at hughsie dot com>
 */
static GKeyFile *
lr_load_multiline_key_file(const char *filename,
                           GError **err)
{
    GKeyFile *file = NULL;
    gboolean ret;
    gsize len;
    guint i;
    _cleanup_error_free_ GError *tmp_err = NULL;
    _cleanup_free_ gchar *data = NULL;
    _cleanup_string_free_ GString *string = NULL;
    _cleanup_strv_free_ gchar **lines = NULL;

    // load file
    if (!g_file_get_contents (filename, &data, &len, &tmp_err)) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_FILE,
                    "Cannot load content of %s: %s",
                    filename, tmp_err->message);
        return NULL;
    }

    // split into lines
    string = g_string_new ("");
    lines = g_strsplit (data, "\n", -1);
    for (i = 0; lines[i] != NULL; i++) {

        // convert tabs to spaces
        g_strdelimit (lines[i], "\t", ' ');

        // if a line starts with whitespace, then append it on
        // the previous line
        if (lines[i][0] == ' ' && string->len > 0) {

            // remove old newline from previous line
            g_string_set_size (string, string->len - 1);

            // whitespace strip this new line
            g_strchug (lines[i]);

            // only add a ';' if we have anything after the '='
            if (string->str[string->len - 1] == '=') {
                g_string_append_printf (string, "%s\n", lines[i]);
            } else {
                g_string_append_printf (string, ";%s\n", lines[i]);
            }
        } else {
            g_string_append_printf (string, "%s\n", lines[i]);
        }
    }

    // remove final newline
    if (string->len > 0)
        g_string_set_size (string, string->len - 1);

    // load modified lines
    file = g_key_file_new ();
    ret = g_key_file_load_from_data (file,
                                     string->str,
                                     -1,
                                     G_KEY_FILE_KEEP_COMMENTS,
                                     &tmp_err);
    if (!ret) {
        g_key_file_free (file);
        g_set_error(err, LR_REPOCONF_ERROR, LRE_KEYFILE,
                    "Cannot parse key file %s: %s", filename, tmp_err->message);
        return NULL;
    }
    return file;
}

static gboolean
lr_key_file_get_boolean(GKeyFile *keyfile,
                        const gchar *groupname,
                        const gchar *key,
                        gboolean default_value,
                        GError **err)
{
    _cleanup_free_ gchar *string = NULL;
    _cleanup_free_ gchar *string_lower = NULL;
    string = g_key_file_get_string(keyfile, groupname, key, err);
    if (!string)
        return default_value;
    string_lower = g_ascii_strdown (string, -1);
    if (!g_strcmp0(string_lower, "1") ||
        !g_strcmp0(string_lower, "yes") ||
        !g_strcmp0(string_lower, "true"))
        return TRUE;
    return FALSE;
}

static long
lr_key_file_get_boolean_long(GKeyFile *keyfile,
                             const gchar *groupname,
                             const gchar *key,
                             gboolean default_value,
                             GError **err)
{
    if (lr_key_file_get_boolean(keyfile, groupname, key, default_value, err))
        return 1;
    return 0;
}

/*
static gint
lr_key_file_get_integer(GKeyFile *keyfile,
                        const gchar *groupname,
                        const gchar *key,
                        gint default_value,
                        GError **err)
{
    if (g_key_file_has_key (keyfile, groupname, key, NULL))
        return g_key_file_get_integer(keyfile, groupname, key, err);
    return default_value;
}
*/

static gchar **
lr_key_file_get_string_list(GKeyFile *keyfile,
                            const gchar *groupname,
                            const gchar *key,
                            GError **err)
{
    gchar **list = NULL;
    _cleanup_free_ gchar *string = NULL;
    string = g_key_file_get_string(keyfile, groupname, key, err);
    if (!string)
        return list;
    list = g_strsplit_set(string, " ,;", 0);
    for (gint i=0; list && list[i]; i++)
        g_strstrip(list[i]);
    return list;
}

static LrIpResolveType
lr_key_file_get_ip_resolve(GKeyFile *keyfile,
                            const gchar *groupname,
                            const gchar *key,
                            LrIpResolveType default_value,
                            GError **err)
{
    _cleanup_free_ gchar *string = NULL;
    _cleanup_free_ gchar *string_lower = NULL;
    string = g_key_file_get_string(keyfile, groupname, key, err);
    if (!string)
        return default_value;
    string_lower = g_ascii_strdown(string, -1);
    if (!g_strcmp0(string_lower, "ipv4"))
        return LR_IPRESOLVE_V4;
    else if (!g_strcmp0(string_lower, "ipv6"))
        return LR_IPRESOLVE_V6;
    else if (!g_strcmp0(string_lower, "whatever"))
        return LR_IPRESOLVE_WHATEVER;
    else
        g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                    "Unknown ip_resolve value '%s'", string);
    return default_value;
}

static void
lr_key_file_set_ip_resolve(GKeyFile *keyfile,
                           const gchar *groupname,
                           const gchar *key,
                           LrIpResolveType resolve_type)
{
    if (resolve_type == LR_IPRESOLVE_WHATEVER)
        g_key_file_set_string(keyfile, groupname, key, "whatever");
    else if (resolve_type == LR_IPRESOLVE_V4)
        g_key_file_set_string(keyfile, groupname, key, "ipv4");
    else if (resolve_type == LR_IPRESOLVE_V6)
        g_key_file_set_string(keyfile, groupname, key, "ipv6");
}

static gboolean
lr_convert_interval_to_seconds(const char *str,
                               gint64 *out,
                               GError **err)
{
    gdouble value = 0.0;
    gdouble mult = 1.0;
    gchar *endptr = NULL;

    *out = 0;

    // Initial sanity checking
    if (!str) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_BADFUNCARG,
                    "No time interval value specified");
        return FALSE;
    }

    // Initial conversion
    value = g_ascii_strtod(str, &endptr);
    if (value == HUGE_VAL && errno == ERANGE) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                    "Too big time interval value '%s'", str);
        return FALSE;
    }

    // String doesn't start with numbers
    if (endptr == str) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                    "Could't convert '%s' to seconds", str);
        return FALSE;
    }

    // Process multiplier (if supplied)
    if (endptr) {
        if (strlen(endptr) != 1) {
            g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                        "Unknown time interval unit '%s'", endptr);
            return FALSE;
        }

        gchar unit = g_ascii_tolower(*endptr);
        if (unit == 's') {
            mult = 1.0;
        } else if (unit == 'm') {
            mult = 60.0;
        } else if (unit == 'h') {
            mult = 60.0 * 60.0;
        } else if (unit == 'd') {
            mult = 60.0 * 60.0 * 24.0;
        } else {
            g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                        "Unknown time interval unit '%s'", endptr);
            return FALSE;
        }
    }

    // Convert result to seconds
    value = value * mult;

    // Return result as integer
    *out = (gint64) value;
    return TRUE;
}

static gint64
lr_key_file_get_metadata_expire(GKeyFile *keyfile,
                                const gchar *groupname,
                                const gchar *key,
                                gint64 default_value,
                                GError **err)
{
    gint64 res = -1;
    _cleanup_free_ gchar *string = NULL;
    string = g_key_file_get_string(keyfile, groupname, key, err);
    if (!string)
        return default_value;
    if (!lr_convert_interval_to_seconds(string, &(res), err))
        return -1;
    return res;
}

static gboolean
lr_convert_bandwidth_to_bytes(const char *str,
                              guint64 *out,
                              GError **err)
{
    gdouble dbytes = 0.0;
    gdouble mult = 1.0;
    gchar *endptr = NULL;

    *out = 0;

    // Initial sanity checking
    if (!str) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_BADFUNCARG,
                    "No bandwidth value specified");
        return FALSE;
    }

    // Initial conversion
    dbytes = g_ascii_strtod(str, &endptr);
    if (dbytes == HUGE_VAL && errno == ERANGE) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                    "Too big bandwidth value '%s'", str);
        return FALSE;
    }

    // String doesn't start with numbers
    if (endptr == str) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                    "Could't convert '%s' to number", str);
        return FALSE;
    }

    // Process multiplier (if supplied)
    if (endptr) {
        if (strlen(endptr) != 1) {
            g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                        "Unknown unit '%s'", endptr);
            return FALSE;
        }

        gchar unit = g_ascii_tolower(*endptr);
        if (unit == 'k') {
            mult = 1024.0;
        } else if (unit == 'm') {
            mult = 1024.0 * 1024.0;
        } else if (unit == 'g') {
            mult = 1024.0 * 1024.0 * 1024.0;
        } else {
            g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                        "Unknown unit '%s'", endptr);
            return FALSE;
        }
    }

    // Convert result to bytes
    dbytes = dbytes * mult;
    if (dbytes < 0.0) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_VALUE,
                    "Bytes value may not be negative '%s'", str);
        return FALSE;
    }

    // Return result as integer
    *out = (guint64) dbytes;
    return TRUE;
}

static guint64
lr_key_file_get_bandwidth(GKeyFile *keyfile,
                          const gchar *groupname,
                          const gchar *key,
                          guint64 default_value,
                          GError **err)
{
    guint64 res = 0;
    _cleanup_free_ gchar *string = NULL;
    string = g_key_file_get_string(keyfile, groupname, key, err);
    if (!string)
        return default_value;
    if (!lr_convert_bandwidth_to_bytes(string, &(res), err))
        return 0;
    return res;
}

static void
lr_key_file_set_string(GKeyFile *keyfile,
                       const gchar *groupname,
                       const gchar *key,
                       const gchar *str)
{
    if (!str) {
        g_key_file_remove_key(keyfile, groupname, key, NULL);
        return;
    }

    g_key_file_set_string(keyfile, groupname, key, str);
}

static void
lr_key_file_set_string_list(GKeyFile *keyfile,
                            const gchar *groupname,
                            const gchar *key,
                            const gchar **list)
{
    gsize len = 0;

    if (list)
        len = g_strv_length((gchar **) list);

    if (len == 0) {
        g_key_file_remove_key(keyfile, groupname, key, NULL);
        return;
    }

    g_key_file_set_string_list(keyfile, groupname, key, (const gchar * const*) list, len);
}

/**
static gboolean
lr_yum_repoconf_parse_id(LrYumRepoConf **yumconf,
                         const gchar *id,
                         const gchar *filename,
                         GKeyFile *keyfile,
                         G_GNUC_UNUSED GError **err)
{
    LrYumRepoConf *conf = lr_yum_repoconf_init();
    _cleanup_error_free_ GError *tmp_err = NULL;

    // _source
    conf->_source = g_strdup(filename);

    // id
    conf->id = g_strdup(id);

    // name
    conf->name = g_key_file_get_string(keyfile, id, "name", NULL);

    // enabled
    conf->enabled = lr_key_file_get_boolean (keyfile, id, "enabled", TRUE, NULL);

    // baseurl
    conf->baseurl = lr_key_file_get_string_list(keyfile, id, "baseurl", NULL);

    // mirrorlist
    conf->mirrorlist = g_key_file_get_string(keyfile, id, "mirrorlist", NULL);

    // metalink
    conf->metalink = g_key_file_get_string(keyfile, id, "metalink", NULL);

    // mediaid
    conf->mediaid = g_key_file_get_string(keyfile, id, "mediaid", NULL);

    // gpgkey
    conf->gpgkey = lr_key_file_get_string_list(keyfile, id, "gpgkey", NULL);

    // gpgcakey
    conf->gpgcakey = lr_key_file_get_string_list(keyfile, id, "gpgcakey", NULL);

    // exclude
    conf->exclude = lr_key_file_get_string_list(keyfile, id, "exclude", NULL);

    // include
    conf->include = lr_key_file_get_string_list(keyfile, id, "include", NULL);

    // fastestmirror
    conf->fastestmirror = lr_key_file_get_boolean (keyfile, id, "fastestmirror", FALSE, NULL);

    // proxy
    conf->proxy = g_key_file_get_string(keyfile, id, "proxy", NULL);

    // proxy_username
    conf->proxy_username = g_key_file_get_string(keyfile, id, "proxy_username", NULL);

    // proxy_password
    conf->proxy_password = g_key_file_get_string(keyfile, id, "proxy_password", NULL);

    // username
    conf->username = g_key_file_get_string(keyfile, id, "username", NULL);

    // password
    conf->password = g_key_file_get_string(keyfile, id, "password", NULL);

    // gpgcheck
    conf->gpgcheck = lr_key_file_get_boolean (keyfile, id, "gpgcheck", FALSE, NULL);

    // repo_gpgcheck
    conf->repo_gpgcheck = lr_key_file_get_boolean (keyfile, id, "repo_gpgcheck", FALSE, NULL);

    // enablegroups
    conf->enablegroups = lr_key_file_get_boolean (keyfile, id, "enablegroups", TRUE, NULL);

    // bandwidth
    conf->bandwidth = lr_key_file_get_bandwidth(keyfile, id, "bandwidth", LR_YUMREPOCONF_BANDWIDTH_DEFAULT, &tmp_err);
    if (tmp_err)
        goto err;

    // throttle
    conf->throttle = g_key_file_get_string(keyfile, id, "throttle", NULL);

    // ip_resolve
    conf->ip_resolve = lr_key_file_get_ip_resolve(keyfile, id, "ip_resolve", LR_YUMREPOCONF_IP_RESOLVE_DEFAULT, NULL);

    // metadata_expire
    conf->metadata_expire = lr_key_file_get_metadata_expire(keyfile, id, "metadata_expire", LR_YUMREPOCONF_METADATA_EXPIRE_DEFAULT, NULL);

    // cost
    conf->cost = lr_key_file_get_integer(keyfile, id, "cost", LR_YUMREPOCONF_COST_DEFAULT, NULL);

    // priority
    conf->priority = lr_key_file_get_integer(keyfile, id, "priority", LR_YUMREPOCONF_PRIORITY_DEFAULT, NULL);

    // sslcacert
    conf->sslcacert = g_key_file_get_string(keyfile, id, "sslcacert", NULL);

    // sslverify
    conf->sslverify = lr_key_file_get_boolean (keyfile, id, "sslverify", TRUE, NULL);

    // sslclientcert
    conf->sslclientcert = g_key_file_get_string(keyfile, id, "sslclientcert", NULL);

    // sslclientkey
    conf->sslclientkey = g_key_file_get_string(keyfile, id, "sslclientkey", NULL);

    // deltarepobaseurl
    conf->deltarepobaseurl = lr_key_file_get_string_list(keyfile, id, "deltarepobaseurl", NULL);

    *yumconf = conf;
    return TRUE;

err:
    lr_yum_repoconf_free(conf);
    *yumconf = NULL;
    return FALSE;
}
*/

gboolean
lr_yum_repoconfs_parse(LrYumRepoConfs *repos,
                       const char *filename,
                       GError **err)
{
    _cleanup_strv_free_ gchar **groups = NULL;
    LrYumRepoFile *repofile = NULL;
    GKeyFile *keyfile = NULL;

    // Load key file content
    keyfile = lr_load_multiline_key_file(filename, err);
    if (!keyfile)
        return FALSE;

    // Create LrYumRepoFile object
    repofile = lr_yum_repofile_init(filename, keyfile);
    repos->files = g_slist_append(repos->files, repofile);

    // Create LrYumRepoConf objects
    groups = g_key_file_get_groups (keyfile, NULL);
    for (guint i = 0; groups[i]; i++) {
        LrYumRepoConf *repoconf = NULL;
        repoconf = lr_yum_repoconf_init(repofile, groups[i]);
        if (!repoconf)
            return FALSE;
        repos->repos = g_slist_append(repos->repos, repoconf);
    }

    return TRUE;
}

gboolean
lr_yum_repoconfs_load_dir(LrYumRepoConfs *repos,
                          const char *path,
                          GError **err)
{
    const gchar *file;
    _cleanup_error_free_ GError *tmp_err = NULL;
    _cleanup_dir_close_ GDir *dir = NULL;

    // Open dir
    dir = g_dir_open(path, 0, &tmp_err);
    if (!dir) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_KEYFILE,
                    "Cannot open dir %s: %s", path, tmp_err->message);
        return FALSE;
    }

    // Find all the .repo files
    while ((file = g_dir_read_name(dir))) {
        _cleanup_free_ gchar *path_tmp = NULL;
        if (!g_str_has_suffix(file, ".repo"))
            continue;
        path_tmp = g_build_filename(path, file, NULL);
        if (!lr_yum_repoconfs_parse(repos, path_tmp, err))
            return FALSE;
    }

    return TRUE;
}

gboolean
lr_yumrepoconf_getinfo(LrYumRepoConf *repoconf,
                       GError **err,
                       LrYumRepoConfOption option,
                       ...)
{
    GError *tmp_err = NULL;
    va_list arg;

    char **str;
    char ***strv;
    long *lnum;

    assert(!err || *err == NULL);

    if (!repoconf) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_BADFUNCARG,
                    "No config specified");
        return FALSE;
    }

    // Shortcuts
    GKeyFile *keyfile = repoconf_keyfile(repoconf);
    gchar *id = repoconf_id(repoconf);

    // Basic sanity checks
    if (!keyfile) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_BADFUNCARG,
                    "No keyfile available in yumrepoconf");
        return FALSE;
    }

    va_start(arg, option);

    switch (option) {

    case LR_YRC_ID:              /*!< (char *) ID (short name) of the repo */
        str = va_arg(arg, char **);
        *str = g_strdup(id);
        break;

    case LR_YRC_NAME:            /*!< (char *) Pretty name of the repo */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "name", &tmp_err);
        break;

    case LR_YRC_ENABLED:         /*!< (long 1 or 0) Is repo enabled? */
        lnum = va_arg(arg, long *);
        *lnum = lr_key_file_get_boolean_long(keyfile, id, "enabled", TRUE, &tmp_err);
        break;

    case LR_YRC_BASEURL:         /*!< (char **) List of base URLs */
        strv = va_arg(arg, char ***);
        *strv = lr_key_file_get_string_list(keyfile, id, "baseurl", &tmp_err);
        break;

    case LR_YRC_MIRRORLIST:      /*!< (char *) Mirrorlist URL */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "mirrorlist", &tmp_err);
        break;

    case LR_YRC_METALINK:        /*!< (char *) Metalink URL */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "metalink", &tmp_err);
        break;

    case LR_YRC_MEDIAID:         /*!< (char *) Media ID */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "mediaid", &tmp_err);
        break;

    case LR_YRC_GPGKEY:          /*!< (char **) URL of GPG key */
        strv = va_arg(arg, char ***);
        *strv = lr_key_file_get_string_list(keyfile, id, "gpgkey", &tmp_err);
        break;

    case LR_YRC_GPGCAKEY:        /*!< (char **) GPG CA key */
        strv = va_arg(arg, char ***);
        *strv = lr_key_file_get_string_list(keyfile, id, "gpgcakey", &tmp_err);
        break;

    case LR_YRC_EXCLUDE:         /*!< (char **) List of exluded packages */
        strv = va_arg(arg, char ***);
        *strv = lr_key_file_get_string_list(keyfile, id, "exclude", &tmp_err);
        break;

    case LR_YRC_INCLUDE:         /*!< (char **) List of included packages */
        strv = va_arg(arg, char ***);
        *strv = lr_key_file_get_string_list(keyfile, id, "include", &tmp_err);
        break;

    case LR_YRC_FASTESTMIRROR:  /*!< (long 1 or 0) Fastest mirror determination */
        lnum = va_arg(arg, long *);
        *lnum = lr_key_file_get_boolean_long(keyfile, id, "fastestmirror", TRUE, &tmp_err);
        break;

    case LR_YRC_PROXY:          /*!< (char *) Proxy addres */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "proxy", &tmp_err);
        break;

    case LR_YRC_PROXY_USERNAME: /*!< (char *) Proxy username */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "proxy_username", &tmp_err);
        break;

    case LR_YRC_PROXY_PASSWORD: /*!< (char *) Proxy password */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "proxy_password", &tmp_err);
        break;

    case LR_YRC_USERNAME:       /*!< (char *) Username */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "username", &tmp_err);
        break;

    case LR_YRC_PASSWORD:       /*!< (char *) Password */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "password", &tmp_err);
        break;

    case LR_YRC_GPGCHECK:       /*!< (long 1 or 0) GPG check for packages */
        lnum = va_arg(arg, long *);
        *lnum = lr_key_file_get_boolean_long(keyfile, id, "gpgcheck", TRUE, &tmp_err);
        break;

    case LR_YRC_REPO_GPGCHECK:  /*!< (long 1 or 0) GPG check for repodata */
        lnum = va_arg(arg, long *);
        *lnum = lr_key_file_get_boolean_long(keyfile, id, "repo_gpgcheck", TRUE, &tmp_err);
        break;

    case LR_YRC_ENABLEGROUPS:   /*!< (long 1 or 0) Use groups */
        lnum = va_arg(arg, long *);
        *lnum = lr_key_file_get_boolean_long(keyfile, id, "enablegroups", TRUE, &tmp_err);
        break;

    case LR_YRC_BANDWIDTH:      /*!< (guint64) Bandwidth - Number of bytes */
    {
        guint64 *num = va_arg(arg, guint64 *);
        *num = lr_key_file_get_bandwidth(keyfile, id, "bandwidth", TRUE, &tmp_err);
        break;
    }

    case LR_YRC_THROTTLE:       /*!< (char *) Throttle string */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "throttle", &tmp_err);
        break;

    case LR_YRC_IP_RESOLVE:     /*!< (LrIpResolveType) Ip resolve type */
    {
        LrIpResolveType *val = va_arg(arg, LrIpResolveType *);
        *val = lr_key_file_get_ip_resolve(keyfile, id, "ip_resolve", LR_YUMREPOCONF_IP_RESOLVE_DEFAULT, &tmp_err);
        break;
    }

    case LR_YRC_METADATA_EXPIRE:/*!< (gint64) Interval in secs for metadata expiration */
    {
        gint64 *num = va_arg(arg, gint64 *);
        *num = lr_key_file_get_metadata_expire(keyfile, id, "metadata_expire", LR_YUMREPOCONF_METADATA_EXPIRE_DEFAULT, &tmp_err);
        break;
    }

    case LR_YRC_COST:           /*!< (gint) Repo cost */
    {
        gint *num = va_arg(arg, gint *);
        *num = g_key_file_get_integer(keyfile, id, "cost", &tmp_err);
        break;
    }

    case LR_YRC_PRIORITY:       /*!< (gint) Repo priority */
    {
        gint *num = va_arg(arg, gint *);
        *num = g_key_file_get_integer(keyfile, id, "priority", &tmp_err);
        break;
    }

    case LR_YRC_SSLCACERT:      /*!< (gchar *) SSL Certification authority cert */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "sslcacert", &tmp_err);
        break;

    case LR_YRC_SSLVERIFY:      /*!< (long 1 or 0) SSL verification */
        lnum = va_arg(arg, long *);
        *lnum = lr_key_file_get_boolean_long(keyfile, id, "sslverify", TRUE, &tmp_err);
        break;

    case LR_YRC_SSLCLIENTCERT:  /*!< (gchar *) SSL Client certificate */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "sslclientcert", &tmp_err);
        break;

    case LR_YRC_SSLCLIENTKEY:   /*!< (gchar *) SSL Client key */
        str = va_arg(arg, char **);
        *str = g_key_file_get_string(keyfile, id, "sslclientkey", &tmp_err);
        break;

    case LR_YRC_DELTAREPOBASEURL:/*!< (char **) Deltarepo mirror URLs */
        strv = va_arg(arg, char ***);
        *strv = lr_key_file_get_string_list(keyfile, id, "deltarepobaseurl", &tmp_err);
        break;

    }

    va_end(arg);

    if (tmp_err) {
        if (tmp_err->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND)
            g_set_error(err, LR_REPOCONF_ERROR, LRE_NOTSET,
                        "Value of option %d is not set",
                        option);
        else
            g_set_error(err, LR_REPOCONF_ERROR, LRE_KEYFILE,
                        "Cannot get value of option %d: %s",
                        option, tmp_err->message);

        return FALSE;
    }

    return TRUE;
}

gboolean
lr_yumrepoconf_setopt(LrYumRepoConf *repoconf,
                      GError **err,
                      LrYumRepoConfOption option,
                      ...)
{
    GError *tmp_err = NULL;
    va_list arg;

    char *str;
    char **strv;
    long lnum;

    assert(!err || *err == NULL);

    if (!repoconf) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_BADFUNCARG,
                    "No config specified");
        return FALSE;
    }

    // Shortcuts
    GKeyFile *keyfile = repoconf_keyfile(repoconf);
    gchar *id = repoconf_id(repoconf);

    // Basic sanity checks
    if (!keyfile) {
        g_set_error(err, LR_REPOCONF_ERROR, LRE_BADFUNCARG,
                    "No keyfile available in yumrepoconf");
        return FALSE;
    }

    va_start(arg, option);

    switch (option) {

    case LR_YRC_ID:              /*!<  0 (char *) ID (short name) of the repo */
        g_set_error(&tmp_err, LR_REPOCONF_ERROR, LRE_BADOPTARG,
                    "ID is read only option");
        break;

    case LR_YRC_NAME:            /*!<  1 (char *) Pretty name of the repo */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "name", str);
        break;

    case LR_YRC_ENABLED:         /*!<  2 (long 1 or 0) Is repo enabled? */
        lnum = va_arg(arg, long);
        g_key_file_set_boolean(keyfile, id, "enabled", lnum ? TRUE : FALSE);
        break;

    case LR_YRC_BASEURL:         /*!<  3 (char **) List of base URLs */
        strv = va_arg(arg, char **);
        lr_key_file_set_string_list(keyfile, id, "baseurl", (const gchar **) strv);
        break;

    case LR_YRC_MIRRORLIST:      /*!<  4 (char *) Mirrorlist URL */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "mirrorlist", str);
        break;

    case LR_YRC_METALINK:        /*!<  5 (char *) Metalink URL */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "metalink", str);
        break;

    case LR_YRC_MEDIAID:         /*!<  6 (char *) Media ID */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "mediaid", str);
        break;

    case LR_YRC_GPGKEY:          /*!<  7 (char **) URL of GPG key */
        strv = va_arg(arg, char **);
        lr_key_file_set_string_list(keyfile, id, "gpgkey", (const gchar **) strv);
        break;

    case LR_YRC_GPGCAKEY:        /*!<  8 (char **) GPG CA key */
        strv = va_arg(arg, char **);
        lr_key_file_set_string_list(keyfile, id, "gpgcakey", (const gchar **) strv);
        break;

    case LR_YRC_EXCLUDE:         /*!<  9 (char **) List of exluded packages */
        strv = va_arg(arg, char **);
        lr_key_file_set_string_list(keyfile, id, "exclude", (const gchar **) strv);
        break;

    case LR_YRC_INCLUDE:         /*!< 10 (char **) List of included packages */
        strv = va_arg(arg, char **);
        lr_key_file_set_string_list(keyfile, id, "include", (const gchar **) strv);
        break;

    case LR_YRC_FASTESTMIRROR:   /*!< 11 (long 1 or 0) Fastest mirror determination */
        strv = va_arg(arg, char **);
        lr_key_file_set_string_list(keyfile, id, "fastestmirror", (const gchar **) strv);
        break;

    case LR_YRC_PROXY:           /*!< 12 (char *) Proxy addres */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "proxy", str);
        break;

    case LR_YRC_PROXY_USERNAME:  /*!< 13 (char *) Proxy username */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "proxy_username", str);
        break;

    case LR_YRC_PROXY_PASSWORD:  /*!< 14 (char *) Proxy password */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "proxy_password", str);
        break;

    case LR_YRC_USERNAME:        /*!< 15 (char *) Username */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "username", str);
        break;

    case LR_YRC_PASSWORD:        /*!< 16 (char *) Password */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "password", str);
        break;

    case LR_YRC_GPGCHECK:        /*!< 17 (long 1 or 0) GPG check for packages */
        lnum = va_arg(arg, long);
        g_key_file_set_boolean(keyfile, id, "gpgcheck", (gboolean) lnum);
        break;

    case LR_YRC_REPO_GPGCHECK:   /*!< 18 (long 1 or 0) GPG check for repodata */
        lnum = va_arg(arg, long);
        g_key_file_set_boolean(keyfile, id, "repo_gpgcheck", (gboolean) lnum);
        break;

    case LR_YRC_ENABLEGROUPS:    /*!< 19 (long 1 or 0) Use groups */
        lnum = va_arg(arg, long);
        g_key_file_set_boolean(keyfile, id, "enablegroups", (gboolean) lnum);
        break;

    case LR_YRC_BANDWIDTH:       /*!< 20 (guint64) Bandwidth - Number of bytes */
    {
        guint64 val = va_arg(arg, guint64);
        g_key_file_set_uint64(keyfile, id, "bandwidth", val);
        break;
    }

    case LR_YRC_THROTTLE:        /*!< 21 (char *) Throttle string */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "throttle", str);
        break;

    case LR_YRC_IP_RESOLVE:      /*!< 22 (LrIpResolveType) Ip resolve type */
    {
        LrIpResolveType val = va_arg(arg, LrIpResolveType);
        lr_key_file_set_ip_resolve(keyfile, id, "ip_resolve", val);
        break;
    }

    case LR_YRC_METADATA_EXPIRE: /*!< 23 (gint64) Interval in secs for metadata expiration */
    {
        // TODO: Store value in appropriate units
        gint64 val = va_arg(arg, gint64);
        g_key_file_set_int64(keyfile, id, "metadata_expire", val);
        break;
    }

    case LR_YRC_COST:            /*!< 24 (gint) Repo cost */
    {
        gint val = va_arg(arg, gint);
        g_key_file_set_integer(keyfile, id, "cost", val);
        break;
    }

    case LR_YRC_PRIORITY:        /*!< 25 (gint) Repo priority */
    {
        guint val = va_arg(arg, gint);
        g_key_file_set_integer(keyfile, id, "priority", val);
        break;
    }

    case LR_YRC_SSLCACERT:       /*!< 26 (gchar *) SSL Certification authority cert */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "sslcacert", str);
        break;

    case LR_YRC_SSLVERIFY:       /*!< 27 (long 1 or 0) SSL verification */
        lnum = va_arg(arg, long);
        g_key_file_set_boolean(keyfile, id, "sslverify", (gboolean) lnum);
        break;

    case LR_YRC_SSLCLIENTCERT:   /*!< 28 (gchar *) SSL Client certificate */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "sslclientcert", str);
        break;

    case LR_YRC_SSLCLIENTKEY:    /*!< 29 (gchar *) SSL Client key */
        str = va_arg(arg, char *);
        lr_key_file_set_string(keyfile, id, "sslclientkey", str);
        break;

    case LR_YRC_DELTAREPOBASEURL:/*!< (char **) Deltarepo mirror URLs */
        strv = va_arg(arg, char **);
        lr_key_file_set_string_list(keyfile, id, "deltarepobaseurl", (const gchar **) strv);
        break;
    }

    va_end(arg);

    if (tmp_err) {
        g_propagate_error(err, tmp_err);
        return FALSE;
    }

    return TRUE;
}
