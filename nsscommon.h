#ifndef NSS_COMMON_H
#define NSS_COMMON_H 1

/* Used for parameters with default C++ values which are also called from C */
#if defined(c_plusplus) || defined(__cplusplus)
#define INIT(v,i) v = (i)
#else
#define INIT(v,i) v
#endif

/* These functions are called from both C and C++.  */
#if defined(c_plusplus) || defined(__cplusplus)
extern "C" {
#endif

#include <pk11func.h>
#include <libintl.h>
#include <locale.h>
#include "config.h"

#if ENABLE_NLS
#define _(string) gettext(string)
#define _N(string, string_plural, count) \
        ngettext((string), (string_plural), (count))
#else
#define _(string) (string)
#define _N(string, string_plural, count) \
        ( (count) == 1 ? (string) : (string_plural) )
#endif
#define _F(format, ...) autosprintf(_(format), __VA_ARGS__)
#define _NF(format, format_plural, count, ...) \
        autosprintf(_N((format), (format_plural), (count)), __VA_ARGS__)

char *nssPasswordCallback (PK11SlotInfo *info __attribute ((unused)),
			   PRBool retry __attribute ((unused)),
			   void *arg __attribute ((unused)));

SECStatus nssInit (const char *db_path, INIT (int readWrite, 0), INIT (int issueMessage, 1));
void nssCleanup (const char *db_path);

void nsscommon_error (const char *msg, INIT(int logit, 1));
void nssError (void);

#if defined(c_plusplus) || defined(__cplusplus)
}
#endif

#if defined(c_plusplus) || defined(__cplusplus)
/* These functions are only called from C++ */
#include <string>

const char *server_cert_nickname ();
std::string server_cert_db_path ();
std::string local_client_cert_db_path ();

void nsscommon_error (const std::string &msg, int logit = 1);

void start_log (const char *arg);
bool log_ok ();
void log (const std::string &msg);
void end_log ();

int check_cert (const std::string &db_path, const std::string &nss_cert_name, bool use_db_password = false);
int gen_cert_db (const std::string &db_path, const std::string &extraDnsNames, bool use_password);
SECStatus add_client_cert (const std::string &inFileName, const std::string &db_path);

void sign_file (
  const std::string &db_path,
  const std::string &nss_cert_name,
  const std::string &inputName,
  const std::string &outputName
);

CERTCertList *get_cert_list_from_db (const std::string &cert_nickname);

#endif // defined(c_plusplus) || defined(__cplusplus)

#endif // NSS_COMMON_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
