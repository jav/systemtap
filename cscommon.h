// Common functions and macros used by the compile-server and its client.
#ifndef CSCOMMON_H
#define CSCOMMON_H 1

#if HAVE_NSS
extern "C"
{
#include <ssl.h>
#include <nspr.h>
}
#endif

// Versioning system for the protocol used for communication between the compile-server and client.
// The original version is 1.0. After that, we use the systemtap release number.
//
// By Policy:
//   - All servers are backward compatible with clients. Servers adapt to the protocol version
//     of the client.
//   - All clients are backward compatible with servers. Clients adapt to the protocol version
//     of the server. Warnings are issued for servers lacking features.
//
// Features:
//   Version 1.0
//     Original version
//   Versions 1.6 and higher
//     Client:
//       - Passes localization variables to the server in the file client_tmpdir + "/locale"
//       - Looks for the uprobes module in server_response + "/uprobes"
//       - No longer needs to remove stap's "Keeping temporary directory ..." message from
//         the server's stderr response.
//       - Looks for 'version' tag in server's avahi record and does not automatically connect to
//         an incompatible server. Also prefers newer servers over older ones.
//     Server:
//       - Applies localization variables passed from the client to stap during translation.
//       - Looks for the uprobes module in server_response + "/uprobes"
//       - Uses --tmpdir to specify temp directory to be used by stap, instead of -k, in order to
//         avoid parsing error messages in search of stap's randomly-generated temp dir.
//       - Advertises its protocol version using a 'version' tag in avahi.
//
#define CURRENT_CS_PROTOCOL_VERSION VERSION

struct cs_protocol_version
{
  cs_protocol_version (const char *v = "1.0") : v(0) { *this = v; }
  ~cs_protocol_version ();
  const cs_protocol_version &operator= (const char *v);
  bool operator< (const cs_protocol_version &that) const;

  const char *v;
};

#if HAVE_NSS
struct thread_arg
{
 PRFileDesc *tcpSocket;
 CERTCertificate *cert;
 SECKEYPrivateKey *privKey;
 PRNetAddr addr;
};

extern int read_from_file (const std::string &fname, cs_protocol_version &data);
extern std::string get_cert_serial_number (const CERTCertificate *cert);
#endif

#endif // CSCOMMON_H
