#ifndef __PARSE_COMMAND
#define __PARSE_COMMAND
#include <stddef.h>

#define FNCALL(fn) void (*fn)(char[][30],int)

class Command{
public:
  Command(const char *szcmd, FNCALL(fn) = NULL,  bool auto_reset = false);
  bool found(char c);
  bool found();
  void cmd(char params[][30], int count);
  void reset();
  const char *name(){return m_sz;}
protected:
  bool m_bautoreset ;
  bool m_bmatchfailed ;
  const char *m_sz ;
  const char *m_p ;
  FNCALL(m_fn) ;
};
  
#endif
