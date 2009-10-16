/* scraped from http://www.mozilla.org/projects/security/pki/nss/ref/ssl/sslerr.html */
/* and mozilla security/manager/locales/en-US/chrome/pipnss/nsserrors.properties */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Netscape security libraries.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1994-2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 * ***** END LICENSE BLOCK ***** */
/* For systemtap's purposes, GPLv2 is the winning license. */

NSSYERROR(SSL_ERROR_EXPORT_ONLY_SERVER,"Unable to communicate securely. Peer does not support high-grade encryption.");
NSSYERROR(SSL_ERROR_US_ONLY_SERVER,"Unable to communicate securely. Peer requires high-grade encryption which is not supported.");
NSSYERROR(SSL_ERROR_NO_CYPHER_OVERLAP,"Cannot communicate securely with peer: no common encryption algorithm(s).");
NSSYERROR(SSL_ERROR_NO_CERTIFICATE,"Unable to find the certificate or key necessary for authentication.");
NSSYERROR(SSL_ERROR_BAD_CERTIFICATE,"Unable to communicate securely with peer: peers's certificate was rejected.");
NSSYERROR(SSL_ERROR_BAD_CLIENT,"The server has encountered bad data from the client.");
NSSYERROR(SSL_ERROR_BAD_SERVER,"The client has encountered bad data from the server.");
NSSYERROR(SSL_ERROR_UNSUPPORTED_CERTIFICATE_TYPE,"Unsupported certificate type.");
NSSYERROR(SSL_ERROR_UNSUPPORTED_VERSION,"Peer using unsupported version of security protocol.");
NSSYERROR(SSL_ERROR_WRONG_CERTIFICATE,"Client authentication failed: private key in key database does not correspond to public key in certificate database.");
NSSYERROR(SSL_ERROR_BAD_CERT_DOMAIN,"Unable to communicate securely with peer: requested domain name does not match the server's certificate.");
NSSYERROR(SSL_ERROR_SSL2_DISABLED,"Peer only supports SSL version 2, which is locally disabled.");
NSSYERROR(SSL_ERROR_BAD_MAC_READ,"SSL received a record with an incorrect Message Authentication Code.");
NSSYERROR(SSL_ERROR_BAD_MAC_ALERT,"SSL peer reports incorrect Message Authentication Code.");
NSSYERROR(SSL_ERROR_BAD_CERT_ALERT,"SSL peer cannot verify your certificate.");
NSSYERROR(SSL_ERROR_REVOKED_CERT_ALERT,"SSL peer rejected your certificate as revoked.");
NSSYERROR(SSL_ERROR_EXPIRED_CERT_ALERT,"SSL peer rejected your certificate as expired.");
NSSYERROR(SSL_ERROR_SSL_DISABLED,"Cannot connect: SSL is disabled.");
NSSYERROR(SSL_ERROR_FORTEZZA_PQG,"Cannot connect: SSL peer is in another FORTEZZA domain.");
NSSYERROR(SSL_ERROR_UNKNOWN_CIPHER_SUITE,"An unknown SSL cipher suite has been requested.");
NSSYERROR(SSL_ERROR_NO_CIPHERS_SUPPORTED,"No cipher suites are present and enabled in this program.");
NSSYERROR(SSL_ERROR_BAD_BLOCK_PADDING,"SSL received a record with bad block padding.");
NSSYERROR(SSL_ERROR_RX_RECORD_TOO_LONG,"SSL received a record that exceeded the maximum permissible length.");
NSSYERROR(SSL_ERROR_TX_RECORD_TOO_LONG,"SSL attempted to send a record that exceeded the maximum permissible length.");
NSSYERROR(SSL_ERROR_CLOSE_NOTIFY_ALERT,"SSL peer has closed this connection.");
NSSYERROR(SSL_ERROR_PUB_KEY_SIZE_LIMIT_EXCEEDED,"SSL Server attempted to use domestic-grade public key with export cipher suite.");
NSSYERROR(SSL_ERROR_NO_SERVER_KEY_FOR_ALG,"Server has no key for the attempted key exchange algorithm.");
NSSYERROR(SSL_ERROR_TOKEN_INSERTION_REMOVAL,"PKCS #11 token was inserted or removed while operation was in progress.");
NSSYERROR(SSL_ERROR_TOKEN_SLOT_NOT_FOUND,"No PKCS#11 token could be found to do a required operation.");
NSSYERROR(SSL_ERROR_NO_COMPRESSION_OVERLAP,"Cannot communicate securely with peer: no common compression algorithm(s).");
NSSYERROR(SSL_ERROR_HANDSHAKE_NOT_COMPLETED,"Cannot initiate another SSL handshake until current handshake is complete.");
NSSYERROR(SSL_ERROR_BAD_HANDSHAKE_HASH_VALUE,"Received incorrect handshakes hash values from peer.");
NSSYERROR(SSL_ERROR_CERT_KEA_MISMATCH,"The certificate provided cannot be used with the selected key exchange algorithm.");
NSSYERROR(SSL_ERROR_NO_TRUSTED_SSL_CLIENT_CA,"No certificate authority is trusted for SSL client authentication.");
NSSYERROR(SSL_ERROR_SESSION_NOT_FOUND,"Client's SSL session ID not found in server's session cache.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_HELLO_REQUEST,"SSL received a malformed Hello Request handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_CLIENT_HELLO,"SSL received a malformed Client Hello handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_SERVER_HELLO,"SSL received a malformed Server Hello handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_CERTIFICATE,"SSL received a malformed Certificate handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_SERVER_KEY_EXCH,"SSL received a malformed Server Key Exchange handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_CERT_REQUEST,"SSL received a malformed Certificate Request handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_HELLO_DONE,"SSL received a malformed Server Hello Done handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_CERT_VERIFY,"SSL received a malformed Certificate Verify handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_CLIENT_KEY_EXCH,"SSL received a malformed Client Key Exchange handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_FINISHED,"SSL received a malformed Finished handshake message.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_CHANGE_CIPHER,"SSL received a malformed Change Cipher Spec record.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_ALERT,"SSL received a malformed Alert record.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_HANDSHAKE,"SSL received a malformed Handshake record.");
NSSYERROR(SSL_ERROR_RX_MALFORMED_APPLICATION_DATA,"SSL received a malformed Application Data record.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_HELLO_REQUEST,"SSL received an unexpected Hello Request handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_CLIENT_HELLO,"SSL received an unexpected Client Hello handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_SERVER_HELLO,"SSL received an unexpected Server Hello handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_CERTIFICATE,"SSL received an unexpected Certificate handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_SERVER_KEY_EXCH,"SSL received an unexpected Server Key Exchange handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_CERT_REQUEST,"SSL received an unexpected Certificate Request handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_HELLO_DONE,"SSL received an unexpected Server Hello Done handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_CERT_VERIFY,"SSL received an unexpected Certificate Verify handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_CLIENT_KEY_EXCH,"SSL received an unexpected Client Key Exchange handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_FINISHED,"SSL received an unexpected Finished handshake message.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_CHANGE_CIPHER,"SSL received an unexpected Change Cipher Spec record.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_ALERT,"SSL received an unexpected Alert record.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_HANDSHAKE,"SSL received an unexpected Handshake record.");
NSSYERROR(SSL_ERROR_RX_UNEXPECTED_APPLICATION_DATA,"SSL received an unexpected Application Data record.");
NSSYERROR(SSL_ERROR_RX_UNKNOWN_RECORD_TYPE,"SSL received a record with an unknown content type.");
NSSYERROR(SSL_ERROR_RX_UNKNOWN_HANDSHAKE,"SSL received a handshake message with an unknown message type.");
NSSYERROR(SSL_ERROR_RX_UNKNOWN_ALERT,"SSL received an alert record with an unknown alert description.");
NSSYERROR(SSL_ERROR_HANDSHAKE_UNEXPECTED_ALERT,"SSL peer was not expecting a handshake message it received.");
NSSYERROR(SSL_ERROR_DECOMPRESSION_FAILURE_ALERT,"SSL peer was unable to successfully decompress an SSL record it received.");
NSSYERROR(SSL_ERROR_HANDSHAKE_FAILURE_ALERT,"SSL peer was unable to negotiate an acceptable set of security parameters.");
NSSYERROR(SSL_ERROR_ILLEGAL_PARAMETER_ALERT,"SSL peer rejected a handshake message for unacceptable content.");
NSSYERROR(SSL_ERROR_UNSUPPORTED_CERT_ALERT,"SSL peer does not support certificates of the type it received.");
NSSYERROR(SSL_ERROR_CERTIFICATE_UNKNOWN_ALERT,"SSL peer had some unspecified issue with the certificate it received.");
NSSYERROR(SSL_ERROR_DECRYPTION_FAILED_ALERT,"Peer was unable to decrypt an SSL record it received.");
NSSYERROR(SSL_ERROR_RECORD_OVERFLOW_ALERT,"Peer received an SSL record that was longer than is permitted.");
NSSYERROR(SSL_ERROR_UNKNOWN_CA_ALERT,"Peer does not recognize and trust the CA that issued your certificate.");
NSSYERROR(SSL_ERROR_ACCESS_DENIED_ALERT,"Peer received a valid certificate, but access was denied.");
NSSYERROR(SSL_ERROR_DECODE_ERROR_ALERT,"Peer could not decode an SSL handshake message.");
NSSYERROR(SSL_ERROR_DECRYPT_ERROR_ALERT,"Peer reports failure of signature verification or key exchange.");
NSSYERROR(SSL_ERROR_EXPORT_RESTRICTION_ALERT,"Peer reports negotiation not in compliance with export regulations.");
NSSYERROR(SSL_ERROR_PROTOCOL_VERSION_ALERT,"Peer reports incompatible or unsupported protocol version.");
NSSYERROR(SSL_ERROR_INSUFFICIENT_SECURITY_ALERT,"Server requires ciphers more secure than those supported by client.");
NSSYERROR(SSL_ERROR_INTERNAL_ERROR_ALERT,"Peer reports it experienced an internal error.");
NSSYERROR(SSL_ERROR_USER_CANCELED_ALERT,"Peer user canceled handshake.");
NSSYERROR(SSL_ERROR_NO_RENEGOTIATION_ALERT,"Peer does not permit renegotiation of SSL security parameters.");
NSSYERROR(SSL_ERROR_GENERATE_RANDOM_FAILURE,"SSL experienced a failure of its random number generator.");
NSSYERROR(SSL_ERROR_SIGN_HASHES_FAILURE,"Unable to digitally sign data required to verify your certificate.");
NSSYERROR(SSL_ERROR_EXTRACT_PUBLIC_KEY_FAILURE,"SSL was unable to extract the public key from the peer's certificate.");
NSSYERROR(SSL_ERROR_SERVER_KEY_EXCHANGE_FAILURE,"Unspecified failure while processing SSL Server Key Exchange handshake.");
NSSYERROR(SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE,"Unspecified failure while processing SSL Client Key Exchange handshake.");
NSSYERROR(SSL_ERROR_ENCRYPTION_FAILURE,"Bulk data encryption algorithm failed in selected cipher suite.");
NSSYERROR(SSL_ERROR_DECRYPTION_FAILURE,"Bulk data decryption algorithm failed in selected cipher suite.");
NSSYERROR(SSL_ERROR_MD5_DIGEST_FAILURE,"MD5 digest function failed.");
NSSYERROR(SSL_ERROR_SHA_DIGEST_FAILURE,"SHA - 1 digest function failed.");
NSSYERROR(SSL_ERROR_MAC_COMPUTATION_FAILURE,"Message Authentication Code computation failed.");
NSSYERROR(SSL_ERROR_SYM_KEY_CONTEXT_FAILURE,"Failure to create Symmetric Key context.");
NSSYERROR(SSL_ERROR_SYM_KEY_UNWRAP_FAILURE,"Failure to unwrap the Symmetric key in Client Key Exchange message.");
NSSYERROR(SSL_ERROR_IV_PARAM_FAILURE,"PKCS11 code failed to translate an IV into a param.");
NSSYERROR(SSL_ERROR_INIT_CIPHER_SUITE_FAILURE,"Failed to initialize the selected cipher suite.");
NSSYERROR(SSL_ERROR_SOCKET_WRITE_FAILURE,"Attempt to write encrypted data to underlying socket failed.");
NSSYERROR(SSL_ERROR_SESSION_KEY_GEN_FAILURE,"Failed to generate session keys for SSL session.");
NSSYERROR(SSL_ERROR_SERVER_CACHE_NOT_CONFIGURED,"SSL server cache not configured and not disabled for this socket.");
NSSYERROR(SSL_ERROR_UNSUPPORTED_EXTENSION_ALERT,"SSL peer does not support requested TLS hello extension.");
NSSYERROR(SSL_ERROR_CERTIFICATE_UNOBTAINABLE_ALERT,"SSL peer could not obtain your certificate from the supplied URL.");
NSSYERROR(SSL_ERROR_UNRECOGNIZED_NAME_ALERT,"SSL peer has no certificate for the requested DNS name.");
NSSYERROR(SSL_ERROR_BAD_CERT_STATUS_RESPONSE_ALERT,"SSL peer was unable to get an OCSP response for its certificate.");
NSSYERROR(SSL_ERROR_BAD_CERT_HASH_VALUE_ALERT,"SSL peer reported bad certificate hash value.");
NSSYERROR(SEC_ERROR_IO,"An I / O error occurred during authentication) or an error occurred during crypto operation (other than signature verification).");
NSSYERROR(SEC_ERROR_LIBRARY_FAILURE,"Security library failure.");
NSSYERROR(SEC_ERROR_BAD_DATA,"Security library: received bad data.");
NSSYERROR(SEC_ERROR_OUTPUT_LEN,"Security library: output length error.");
NSSYERROR(SEC_ERROR_INPUT_LEN,"Security library: input length error.");
NSSYERROR(SEC_ERROR_INVALID_ARGS,"Security library: invalid arguments.");
NSSYERROR(SEC_ERROR_INVALID_ALGORITHM,"Security library: invalid algorithm.");
NSSYERROR(SEC_ERROR_INVALID_AVA,"Security library: invalid AVA.");
NSSYERROR(SEC_ERROR_INVALID_TIME,"Security library: invalid time.");
NSSYERROR(SEC_ERROR_BAD_DER,"Security library:improperly formatted DER - encoded message.");
NSSYERROR(SEC_ERROR_BAD_SIGNATURE,"Peer's certificate has an invalid signature.");
NSSYERROR(SEC_ERROR_EXPIRED_CERTIFICATE,"Peer's certificate has expired.");
NSSYERROR(SEC_ERROR_REVOKED_CERTIFICATE,"Peer's certificate has been revoked.");
NSSYERROR(SEC_ERROR_UNKNOWN_ISSUER,"Peer's certificate issuer is not recognized.");
NSSYERROR(SEC_ERROR_BAD_KEY,"Peer's public key is invalid");
NSSYERROR(SEC_ERROR_BAD_PASSWORD,"The password entered is incorrect.");
NSSYERROR(SEC_ERROR_RETRY_PASSWORD,"New password entered incorrectly.");
NSSYERROR(SEC_ERROR_NO_NODELOCK,"Security library: no nodelock.");
NSSYERROR(SEC_ERROR_BAD_DATABASE,"Security library: bad database.");
NSSYERROR(SEC_ERROR_NO_MEMORY,"Security library: memory allocation failure.");
NSSYERROR(SEC_ERROR_UNTRUSTED_ISSUER,"Peer's certificate issuer has been marked as not trusted by the user.");
NSSYERROR(SEC_ERROR_UNTRUSTED_CERT,"Peer's certificate has been marked as not trusted by the user.");
NSSYERROR(SEC_ERROR_DUPLICATE_CERT,"Certificate already exists in your database.");
NSSYERROR(SEC_ERROR_DUPLICATE_CERT_NAME,"Downloaded certificate's name duplicates one already in your database.");
NSSYERROR(SEC_ERROR_ADDING_CERT,"Error adding certificate to database.");
NSSYERROR(SEC_ERROR_FILING_KEY,"Error refiling the key for this certificate.");
NSSYERROR(SEC_ERROR_NO_KEY,"The private key for this certificate cannot be found in key database.");
NSSYERROR(SEC_ERROR_CERT_VALID,"This certificate is valid.");
NSSYERROR(SEC_ERROR_CERT_NOT_VALID,"This certificate is not valid.");
NSSYERROR(SEC_ERROR_CERT_NO_RESPONSE,"Certificate library:no response.");
NSSYERROR(SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE,"The certificate issuer's certificate has expired.");
NSSYERROR(SEC_ERROR_CRL_EXPIRED,"The CRL for the certificate's issuer has expired.");
NSSYERROR(SEC_ERROR_CRL_BAD_SIGNATURE,"The CRL for the certificate's issuer has an invalid signature.");
NSSYERROR(SEC_ERROR_CRL_INVALID,"New CRL has an invalid format.");
NSSYERROR(SEC_ERROR_EXTENSION_VALUE_INVALID,"Certificate extension value is invalid.");
NSSYERROR(SEC_ERROR_EXTENSION_NOT_FOUND,"Certificate extension not found.");
NSSYERROR(SEC_ERROR_CA_CERT_INVALID,"Issuer certificate is invalid.");
NSSYERROR(SEC_ERROR_PATH_LEN_CONSTRAINT_INVALID,"Certificate path length constraint is invalid.");
NSSYERROR(SEC_ERROR_CERT_USAGES_INVALID,"Certificate usages field is invalid.");
NSSYERROR(SEC_INTERNAL_ONLY,"Internal-only module.");
NSSYERROR(SEC_ERROR_INVALID_KEY,"The key does not support the requested operation.");
NSSYERROR(SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION,"Certificate contains unknown critical extension.");
NSSYERROR(SEC_ERROR_OLD_CRL,"New CRL is not later than the current one.");
NSSYERROR(SEC_ERROR_NO_EMAIL_CERT,"Not encrypted or signed: you do not yet have an email certificate.");
NSSYERROR(SEC_ERROR_NO_RECIPIENT_CERTS_QUERY,"Not encrypted: you do not have certificates for each of the recipients.");
NSSYERROR(SEC_ERROR_NOT_A_RECIPIENT,"Cannot decrypt: you are not a recipient, or matching certificate and private key not found.");
NSSYERROR(SEC_ERROR_PKCS7_KEYALG_MISMATCH,"Cannot decrypt: key encryption algorithm does not match your certificate.");
NSSYERROR(SEC_ERROR_PKCS7_BAD_SIGNATURE,"Signature verification failed: no signer found, too many signers found, or improper or corrupted data. ");
NSSYERROR(SEC_ERROR_UNSUPPORTED_KEYALG,"Unsupported or unknown key algorithm.");
NSSYERROR(SEC_ERROR_DECRYPTION_DISALLOWED,"Cannot decrypt: encrypted using a disallowed algorithm or key size.");
NSSYERROR(XP_SEC_FORTEZZA_BAD_CARD,"FORTEZZA card has not been properly initialized.");
NSSYERROR(XP_SEC_FORTEZZA_NO_CARD,"No FORTEZZA cards found.");
NSSYERROR(XP_SEC_FORTEZZA_NONE_SELECTED,"No FORTEZZA card selected.");
NSSYERROR(XP_SEC_FORTEZZA_MORE_INFO,"Please select a personality to get more info on.");
NSSYERROR(XP_SEC_FORTEZZA_PERSON_NOT_FOUND,"Personality not found");
NSSYERROR(XP_SEC_FORTEZZA_NO_MORE_INFO,"No more information on that personality.");
NSSYERROR(XP_SEC_FORTEZZA_BAD_PIN,"Invalid PIN.");
NSSYERROR(XP_SEC_FORTEZZA_PERSON_ERROR,"Couldn' t initialize FORTEZZA personalities.");
NSSYERROR(SEC_ERROR_NO_KRL,"No KRL for this site's certificate has been found.");
NSSYERROR(SEC_ERROR_KRL_EXPIRED,"The KRL for this site's certificate has expired.");
NSSYERROR(SEC_ERROR_KRL_BAD_SIGNATURE,"The KRL for this site's certificate has an invalid signature.");
NSSYERROR(SEC_ERROR_REVOKED_KEY,"The key for this site's certificate has been revoked.");
NSSYERROR(SEC_ERROR_KRL_INVALID,"New KRL has an invalid format.");
NSSYERROR(SEC_ERROR_NEED_RANDOM,"Security library: need random data.");
NSSYERROR(SEC_ERROR_NO_MODULE,"Security library: no security module can perform the requested operation.");
NSSYERROR(SEC_ERROR_NO_TOKEN,"The security card or token does not exist, needs to be initialized, or has been removed.");
NSSYERROR(SEC_ERROR_READ_ONLY,"Security library:read - only database.");
NSSYERROR(SEC_ERROR_NO_SLOT_SELECTED,"No slot or token was selected.");
NSSYERROR(SEC_ERROR_CERT_NICKNAME_COLLISION,"A certificate with the same nickname already exists.");
NSSYERROR(SEC_ERROR_KEY_NICKNAME_COLLISION,"A key with the same nickname already exists. ");
NSSYERROR(SEC_ERROR_SAFE_NOT_CREATED,"Error while creating safe object.");
NSSYERROR(SEC_ERROR_BAGGAGE_NOT_CREATED,"Error while creating baggage object.");
NSSYERROR(XP_JAVA_REMOVE_PRINCIPAL_ERROR,"Couldn 't remove the principal.");
NSSYERROR(XP_JAVA_DELETE_PRIVILEGE_ERROR,"Couldn' t delete the privilege ");
NSSYERROR(XP_JAVA_CERT_NOT_EXISTS_ERROR,"This principal doesn 't have a certificate.");
NSSYERROR(SEC_ERROR_BAD_EXPORT_ALGORITHM,"Required algorithm is not allowed.");
NSSYERROR(SEC_ERROR_EXPORTING_CERTIFICATES,"Error attempting to export certificates.");
NSSYERROR(SEC_ERROR_IMPORTING_CERTIFICATES,"Error attempting to import certificates.");
NSSYERROR(SEC_ERROR_PKCS12_DECODING_PFX,"Unable to import. Decoding error. File not valid.");
NSSYERROR(SEC_ERROR_PKCS12_INVALID_MAC,"Unable to import. Invalid MAC. Incorrect password or corrupt file.");
NSSYERROR(SEC_ERROR_PKCS12_UNSUPPORTED_MAC_ALGORITHM,"Unable to import. MAC algorithm not supported.");
NSSYERROR(SEC_ERROR_PKCS12_UNSUPPORTED_TRANSPORT_MODE,"Unable to import. Only password integrity and privacy modes supported.");
NSSYERROR(SEC_ERROR_PKCS12_CORRUPT_PFX_STRUCTURE,"Unable to import. File structure is corrupt.");
NSSYERROR(SEC_ERROR_PKCS12_UNSUPPORTED_PBE_ALGORITHM,"Unable to import. Encryption algorithm not supported.");
NSSYERROR(SEC_ERROR_PKCS12_UNSUPPORTED_VERSION,"Unable to import. File version not supported.");
NSSYERROR(SEC_ERROR_PKCS12_PRIVACY_PASSWORD_INCORRECT,"Unable to import. Incorrect privacy password.");
NSSYERROR(SEC_ERROR_PKCS12_CERT_COLLISION,"Unable to import. Same nickname already exists in database.");
NSSYERROR(SEC_ERROR_USER_CANCELLED,"The user clicked cancel.");
NSSYERROR(SEC_ERROR_PKCS12_DUPLICATE_DATA,"Not imported, already in database.");
NSSYERROR(SEC_ERROR_MESSAGE_SEND_ABORTED,"Message not sent.");
NSSYERROR(SEC_ERROR_INADEQUATE_KEY_USAGE,"Certificate key usage inadequate for attempted operation.");
NSSYERROR(SEC_ERROR_INADEQUATE_CERT_TYPE,"Certificate type not approved for application.");
NSSYERROR(SEC_ERROR_CERT_ADDR_MISMATCH,"Address in signing certificate does not match address in message headers.");
NSSYERROR(SEC_ERROR_PKCS12_UNABLE_TO_IMPORT_KEY,"Unable to import. Error attempting to import private key.");
NSSYERROR(SEC_ERROR_PKCS12_IMPORTING_CERT_CHAIN,"Unable to import. Error attempting to import certificate chain.");
NSSYERROR(SEC_ERROR_PKCS12_UNABLE_TO_LOCATE_OBJECT_BY_NAME,"Unable to export. Unable to locate certificate or key by nickname.");
NSSYERROR(SEC_ERROR_PKCS12_UNABLE_TO_EXPORT_KEY,"Unable to export. Private key could not be located and exported.");
NSSYERROR(SEC_ERROR_PKCS12_UNABLE_TO_WRITE,"Unable to export. Unable to write the export file.");
NSSYERROR(SEC_ERROR_PKCS12_UNABLE_TO_READ,"Unable to import. Unable to read the import file.");
NSSYERROR(SEC_ERROR_PKCS12_KEY_DATABASE_NOT_INITIALIZED,"Unable to export. Key database corrupt or deleted.");
NSSYERROR(SEC_ERROR_KEYGEN_FAIL,"Unable to generate public-private key pair.");
NSSYERROR(SEC_ERROR_INVALID_PASSWORD,"Password entered is invalid.");
NSSYERROR(SEC_ERROR_RETRY_OLD_PASSWORD,"Old password entered incorrectly.");
NSSYERROR(SEC_ERROR_BAD_NICKNAME,"Certificate nickname already in use.");
NSSYERROR(SEC_ERROR_NOT_FORTEZZA_ISSUER,"Peer FORTEZZA chain has a non-FORTEZZA Certificate.");
NSSYERROR(SEC_ERROR_CANNOT_MOVE_SENSITIVE_KEY,"A sensitive key cannot be moved to the slot where it is needed.");
NSSYERROR(SEC_ERROR_JS_INVALID_MODULE_NAME,"Invalid module name.");
NSSYERROR(SEC_ERROR_JS_INVALID_DLL,"Invalid module path/filename.");
NSSYERROR(SEC_ERROR_JS_ADD_MOD_FAILURE,"Unable to add module.");
NSSYERROR(SEC_ERROR_JS_DEL_MOD_FAILURE,"Unable to delete module.");
NSSYERROR(SEC_ERROR_OLD_KRL,"New KRL is not later than the current one.");
NSSYERROR(SEC_ERROR_CKL_CONFLICT,"New CKL has different issuer than current CKL.");
NSSYERROR(SEC_ERROR_CERT_NOT_IN_NAME_SPACE,"Certificate issuer is not permitted to issue a certificate with this name.");
NSSYERROR(SEC_ERROR_KRL_NOT_YET_VALID,"The key revocation list for this certificate is not yet valid.");
NSSYERROR(SEC_ERROR_CRL_NOT_YET_VALID,"The certificate revocation list for this certificate is not yet valid.");
NSSYERROR(SEC_ERROR_UNKNOWN_CERT,"The requested certificate could not be found.");
NSSYERROR(SEC_ERROR_UNKNOWN_SIGNER,"The signer's certificate could not be found.");
NSSYERROR(SEC_ERROR_CERT_BAD_ACCESS_LOCATION,"The location for the certificate status server has invalid format.");
NSSYERROR(SEC_ERROR_OCSP_UNKNOWN_RESPONSE_TYPE,"The OCSP response cannot be fully decoded) it is of an unknown type.");
NSSYERROR(SEC_ERROR_OCSP_BAD_HTTP_RESPONSE,"The OCSP server returned unexpected / invalid HTTP data.");
NSSYERROR(SEC_ERROR_OCSP_MALFORMED_REQUEST,"The OCSP server found the request to be corrupted or improperly formed.");
NSSYERROR(SEC_ERROR_OCSP_SERVER_ERROR,"The OCSP server experienced an internal error.");
NSSYERROR(SEC_ERROR_OCSP_TRY_SERVER_LATER,"The OCSP server suggests trying again later.");
NSSYERROR(SEC_ERROR_OCSP_REQUEST_NEEDS_SIG,"The OCSP server requires a signature on this request.");
NSSYERROR(SEC_ERROR_OCSP_UNAUTHORIZED_REQUEST,"The OCSP server has refused this request as unauthorized.");
NSSYERROR(SEC_ERROR_OCSP_UNKNOWN_RESPONSE_STATUS,"The OCSP server returned an unrecognizable status.");
NSSYERROR(SEC_ERROR_OCSP_UNKNOWN_CERT,"The OCSP server has no status for the certificate.");
NSSYERROR(SEC_ERROR_OCSP_NOT_ENABLED,"You must enable OCSP before performing this operation.");
NSSYERROR(SEC_ERROR_OCSP_NO_DEFAULT_RESPONDER,"You must set the OCSP default responder before performing this operation.");
NSSYERROR(SEC_ERROR_OCSP_MALFORMED_RESPONSE,"The response from the OCSP server was corrupted or improperly formed.");
NSSYERROR(SEC_ERROR_OCSP_UNAUTHORIZED_RESPONSE,"The signer of the OCSP response is not authorized to give status for this certificate.");
NSSYERROR(SEC_ERROR_OCSP_FUTURE_RESPONSE,"The OCSP response is not yet valid (contains a date in the future) . ");
NSSYERROR(SEC_ERROR_OCSP_OLD_RESPONSE,"The OCSP response contains out - of - date information.");
NSSYERROR(SEC_ERROR_DIGEST_NOT_FOUND,"The CMS or PKCS #7 Digest was not found in signed message.");
NSSYERROR(SEC_ERROR_UNSUPPORTED_MESSAGE_TYPE,"The CMS or PKCS #7 Message type is unsupported.");
NSSYERROR(SEC_ERROR_MODULE_STUCK,"PKCS #11 module could not be removed because it is still in use.");
NSSYERROR(SEC_ERROR_BAD_TEMPLATE,"Could not decode ASN .1 data.Specified template was invalid.");
NSSYERROR(SEC_ERROR_CRL_NOT_FOUND,"No matching CRL was found.");
NSSYERROR(SEC_ERROR_REUSED_ISSUER_AND_SERIAL,"You are attempting to import a cert with the same issuer / serial as an existing cert, but that is not the same cert.");
NSSYERROR(SEC_ERROR_BUSY,"NSS could not shutdown.Objects are still in use.");
NSSYERROR(SEC_ERROR_EXTRA_INPUT,"DER - encoded message contained extra unused data.");
NSSYERROR(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE,"Unsupported elliptic curve.");
NSSYERROR(SEC_ERROR_UNSUPPORTED_EC_POINT_FORM,"Unsupported elliptic curve point form.");
NSSYERROR(SEC_ERROR_UNRECOGNIZED_OID,"Unrecognized Object IDentifier.");
NSSYERROR(SEC_ERROR_OCSP_INVALID_SIGNING_CERT,"Invalid OCSP signing certificate in OCSP response.");
NSSYERROR(SEC_ERROR_REVOKED_CERTIFICATE_CRL,"Certificate is revoked in issuer's certificate revocation list.");
NSSYERROR(SEC_ERROR_REVOKED_CERTIFICATE_OCSP,"Issuer's OCSP responder reports certificate is revoked.");
NSSYERROR(SEC_ERROR_CRL_INVALID_VERSION,"Issuer's Certificate Revocation List has an unknown version number.");
NSSYERROR(SEC_ERROR_CRL_V1_CRITICAL_EXTENSION,"Issuer's V1 Certificate Revocation List has a critical extension.");
NSSYERROR(SEC_ERROR_CRL_UNKNOWN_CRITICAL_EXTENSION,"Issuer's V2 Certificate Revocation List has an unknown critical extension.");
NSSYERROR(SEC_ERROR_UNKNOWN_OBJECT_TYPE,"Unknown object type specified.");
NSSYERROR(SEC_ERROR_INCOMPATIBLE_PKCS11,"PKCS #11 driver violates the spec in an incompatible way.");
NSSYERROR(SEC_ERROR_NO_EVENT,"No new slot event is available at this time.");
NSSYERROR(SEC_ERROR_CRL_ALREADY_EXISTS,"CRL already exists.");
NSSYERROR(SEC_ERROR_NOT_INITIALIZED,"NSS is not initialized.");
NSSYERROR(SEC_ERROR_TOKEN_NOT_LOGGED_IN,"The operation failed because the PKCS #11 token is not logged in.");
NSSYERROR(SEC_ERROR_OCSP_RESPONDER_CERT_INVALID,"The configured OCSP responder's certificate is invalid.");
NSSYERROR(SEC_ERROR_OCSP_BAD_SIGNATURE,"OCSP response has an invalid signature.");
NSSYERROR(SEC_ERROR_OUT_OF_SEARCH_LIMITS,"Certification validation search is out of search limits.");
NSSYERROR(SEC_ERROR_INVALID_POLICY_MAPPING,"Policy mapping contains any - policy.");
NSSYERROR(SEC_ERROR_POLICY_VALIDATION_FAILED,"Certificate chain fails policy validation.");
NSSYERROR(SEC_ERROR_UNKNOWN_AIA_LOCATION_TYPE,"Unknown location type in certificate AIA extension.");
NSSYERROR(SEC_ERROR_BAD_HTTP_RESPONSE,"Server returned a bad HTTP response.");
NSSYERROR(SEC_ERROR_BAD_LDAP_RESPONSE,"Server returned a bad LDAP response.");
NSSYERROR(SEC_ERROR_FAILED_TO_ENCODE_DATA,"Failed to encode data with ASN .1 encoder.");
NSSYERROR(SEC_ERROR_BAD_INFO_ACCESS_LOCATION,"Bad information access location in certificate extension.");
NSSYERROR(SEC_ERROR_LIBPKIX_INTERNAL,"Libpkix internal error occurred during cert validation.");
NSSYERROR(SEC_ERROR_PKCS11_GENERAL_ERROR,"A PKCS #11 module returned CKR_GENERAL_ERROR, indicating that an unrecoverable error has occurred.");
NSSYERROR(SEC_ERROR_PKCS11_FUNCTION_FAILED,"A PKCS #11 module returned CKR_FUNCTION_FAILED, indicating that the requested function could not be performed.Trying the same operation again might succeed.");
NSSYERROR(SEC_ERROR_PKCS11_DEVICE_ERROR,"A PKCS #11 module returned CKR_DEVICE_ERROR, indicating that a problem has occurred with the token or slot.");
/* 3.12.4 and later */
#if (NSS_VMAJOR > 3) || (NSS_VMAJOR == 3 && NSS_VMINOR > 12) || (NSS_VMAJOR == 3 && NSS_VMINOR == 12 && NSS_VPATCH >= 4)
NSSYERROR(SEC_ERROR_BAD_INFO_ACCESS_METHOD,"Unknown information access method in certificate extension.");
NSSYERROR(SEC_ERROR_CRL_IMPORT_FAILED,"Error attempting to import a CRL.");
#endif
