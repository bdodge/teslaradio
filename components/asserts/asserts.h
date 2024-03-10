
#pragma once

#define require(cond, lab)                      if (!(cond))    { assert_err(__FILE__, __LINE__); goto lab; }
#define require_action(cond, lab, action)       if (!(cond))    { assert_err(__FILE__, __LINE__); action; goto lab; }
#define require_action_quiet(cond, lab, action) if (!(cond))    { action; goto lab; }
#define require_noerr(result, lab)              if ((result))   { assert_err(__FILE__, __LINE__); goto lab; }
#define verify(cond)                            if (!(cond))    { assert_err(__FILE__, __LINE__); }
#define verify_noerr(result)                    if ((result))   { assert_err(__FILE__, __LINE__); }

void assert_err(const char *file, const int line);

