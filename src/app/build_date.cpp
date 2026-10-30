// The date the ROM was built: the first cursor day (there is no clock).
// Kept in its own file; extern "C" so tests can find it in the ELF.
extern "C" const char planner_build_date[] = __DATE__;
