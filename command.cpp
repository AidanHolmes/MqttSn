#include "command.hpp"

Command::Command(const char *szcmd, FNCALL(fn), bool auto_reset)
{
  m_sz = szcmd ;
  m_p = m_sz ;
  m_fn = fn ;
  m_bautoreset = auto_reset ;
  m_bmatchfailed = false ;
  reset() ;
}
void Command::reset()
{
  m_p = m_sz ;
  m_bmatchfailed = false ;
}
void Command::cmd(char params[][30], int count)
{
  if (m_fn) (*m_fn)(params, count) ;
}

bool Command::found(char c)
{
  if (*m_p != c){
    if (!m_bautoreset) m_bmatchfailed = true ;
    m_p = m_sz ;
  }else{
    m_p++;
  }
  return found() ;
}
bool Command::found()
{
  if (*m_p == '\0' && !m_bmatchfailed) return true ;
  return false ;
}
