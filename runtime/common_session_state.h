// Will be included once by translate.cxx c_unparser::emit_common_header ().

#define STAP_SESSION_STARTING 0
#define STAP_SESSION_RUNNING 1
#define STAP_SESSION_ERROR 2
#define STAP_SESSION_STOPPING 3
#define STAP_SESSION_STOPPED 4
static atomic_t session_state = ATOMIC_INIT (STAP_SESSION_STARTING);

static atomic_t error_count = ATOMIC_INIT (0);
static atomic_t skipped_count = ATOMIC_INIT (0);
static atomic_t skipped_count_lowstack = ATOMIC_INIT (0);
static atomic_t skipped_count_reentrant = ATOMIC_INIT (0);
static atomic_t skipped_count_uprobe_reg = ATOMIC_INIT (0);
static atomic_t skipped_count_uprobe_unreg = ATOMIC_INIT (0);
