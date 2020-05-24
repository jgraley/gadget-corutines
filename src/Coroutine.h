/**
 * Coroutine.h - Coroutines for Gadgets.
 * Created by John Graley, 2020.
 * (C) John Graley LGPL license applies.
 * 
 * Main Gadget Coroutine class
 */
#ifndef Coroutine_h
#define Coroutine_h

#if __cplusplus <= 199711L
  #error This library needs at least a C++11 compliant compiler
#endif

#include "Coroutine_arm.h"
#include "Task.h"
#include "Integration.h"

#include <functional>
#include <csetjmp> 
#include <cstdint>
#include "Arduino.h"

namespace GC
{

class Coroutine : public Task
{
public:
  explicit Coroutine( std::function<void()> child_function_ ); 
  ~Coroutine();
    
  inline static Coroutine *me();
  inline static void yield();

  static void wait( std::function<bool()> test );
  void set_hop_lambda( std::function<void()> hop );
  std::pair<const byte *, const byte *> get_child_stack_bounds();
  int estimate_stack_peak_usage();
  int get_tls_usage();
  
private:
  enum ChildStatus
  {
    READY,
    RUNNING,
    COMPLETE
  };
  
  enum LongJmpValue
  {
    IMMEDIATE = 0, // Must be zero
    PARENT_TO_CHILD = 1, // All the others must be non-zero
    CHILD_TO_PARENT = 2,
    PARENT_TO_CHILD_STARTING = 3
  };

  // from libgcc/emutls.c
  typedef unsigned int word __attribute__((mode(word)));
  typedef unsigned int pointer __attribute__((mode(pointer)));
  struct __emutls_object
  {
    word size;
    word align;
    union {
      pointer offset;
      void *ptr;
    } loc;
    void *templ;
  };

  byte *prepare_child_stack( byte *frame_end, byte *stack_pointer );
  void prepare_child_jmp_buf( jmp_buf &child_jmp_buf, const jmp_buf &initial_jmp_buf, byte *parent_stack_pointer, byte *child_stack_pointer );
  [[ noreturn ]] void child_main_function();
  void run_iteration();
  void jump_to_child();
  void yield_nonstatic();
  [[ noreturn ]] void jump_to_parent();
  static void *get_tls_pointer(void *obj) asm ("__emutls_get_address");
  
  const std::function<void()> child_function; 
  const int stack_size;
  byte * const child_stack_memory;
  ChildStatus child_status;
  jmp_buf parent_jmp_buf;
  jmp_buf child_jmp_buf;
    
  static int tls_top;
  static byte *tls_outside_heap;
    
  static const int default_stack_size = 2048;
};

///-- 
// Implement the inline functions here

Coroutine *Coroutine::me()
{
  return (Coroutine *)( get_cls() );
}


void Coroutine::yield()
{
  Coroutine * const me_value = me();
  if( me_value )
    me_value->yield_nonstatic();
}

} // namespace

inline GC::Coroutine *me()
{
    return GC::Coroutine::me();
}

#endif
