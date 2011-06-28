#ifndef CSCOMMON_H
#define CSCOMMON_H 1
// Common functions and macros used by the compile-server and its client.

// Versioning system for the compile-server and client.
#define CS_PROTOCOL_VERSION(major, minor) (((major) << 16) | (minor))

// Current protocol version.
// Version 1.0
//   Original version
// Version 1.1
//   Client:
//     - Passes localization variables to the server in the file client_tmpdir + "/locale"
//     - Looks for 'version' tag in server's avahi record and does not automatically connect to
//       an incompatioble server.
//   Server:
//   - Applies localization variables passed from the client to stap during translation.
//   - Uses --tmpdir to specify temp directory to be used by stap, instead of -k, in order to avoid
//     parsing error messages in search of stap's randomly-generated temp dir. As a result, the
//     client no longer needs to remove stap's "Keeping temporary directory ..." message from the
//     server's stderr response.
//   - Advertises its protocol version using a 'version' tag in avahi.
//
#define CURRENT_CS_PROTOCOL_VERSION CS_PROTOCOL_VERSION (1, 1)

inline bool client_server_compatible (int client_version, int server_version)
{
  // A present, all versions of the client and server can deal with one another. However reject
  // future counterparts using a higher protocol version.
  return client_version <= CURRENT_CS_PROTOCOL_VERSION &&
    server_version <= CURRENT_CS_PROTOCOL_VERSION;
}

#endif // CSCOMMON_H
