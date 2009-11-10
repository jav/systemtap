int verify_module (const char *signature_name, const char *module_name,
		   const void *module_data, off_t module_size);

/* return codes for verify_module.  */
#define MODULE_OK           1
#define MODULE_UNTRUSTED    0
#define MODULE_CHECK_ERROR -1
#define MODULE_ALTERED     -2

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
