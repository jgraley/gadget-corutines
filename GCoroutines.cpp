/*
  GCoroutines.h - Coroutines for Gadgets.
  Created by John Graley, 2020.
  (C) John Graley LGPL license applies.
*/

#include "GCoroutines.h"
#include <cstring>
#include <stdexcept>

using namespace std;

#if defined(__arm__) || defined(__thumb__)

#if sizeof(jmp_buf) != 23
#error setjmp.h isnt the one we expected for arm/thumb
#endif

static const int ARM_JMPBUF_INDEX_SP = 8;
inline byte *get_jmp_buf_sp( jmp_buf env )
{
    return static_cast<byte *>( env[ARM_JMPBUF_INDEX_SP] );
}
inline byte set_jmp_buf_sp( jmp_buf env, void *new_sp )
{
    env[ARM_JMPBUF_INDEX_SP] = static_cast<int>(new_sp);
}
#endif

class GCoroutine_internal_error : public runtime_error
{
    explicit runtime_error( const std::string& what_arg ) :
        runtime_error( string("GCoroutines: ") + what_arg )
    {
    }
};


class GCoroutine_bad_longjmp_value : public GCoroutine_internal_error
{
    explicit GCoroutine_bad_longjmp_value(const char *file, int line, int val) :
        GCoroutine_internal_error( string(file) + ":" + to_string(line) + " bad longjmp val: " + to_string(val) )
    {
    }
};


class GCoroutine_TODO : public GCoroutine_internal_error
{
    explicit GCoroutine_TODO(string what) :
        GCoroutine_internal_error( "TODO: " + what )
    {
    }
};


enum
{
    IMMEDIATE = 0, // Must be zero
    PARENT_TO_CHILD = 1, // All the others must be non-zero
    CHILD_TO_PARENT = 2,
    PARENT_TO_CHILD_BOOTING = 3
}


GCoroutine::GCoroutine( function child_function ) :
    child_stack_memory( new byte[default_stack_size] ),
    child_status(READY)
{
    jmp_buf parent_jmp_buf;
    int val;
    switch( val = setjmp(parent_jmp_buf) )
    { 
    case IMMEDIATE:
        // Get current stack pointer and frame address
        byte *stack_pointer = get_jmp_buf_sp(parent_jmp_buf);
        byte *fp = static_cast<byte *>( __builtin_frame_address(0) );
        
        // Decide how much stack to keep (basically the current frame, i.e. the 
        // stack frame of this invocation of this function) and copy it into the 
        // new stack, at the bottom.
        // Note: stacks usually begin at the highest address and work down
        bytes_to_retain = frame_address - stack_pointer;
        byte *new_stack_pointer = new_stack_mem + default_stack_size - bytes_to_retain;      
        memmove( new_stack_pointer, stack_pointer, bytes_to_retain );
        
        // Prepare a jump buffer for the child and point it to the new stack
        memcpy( &child_jmp_buf, &parent_jmp_buf, sizeof(jmp_buf) );
        set_jmp_buf_sp(child_jmp_buf, new_stack_pointer);
        break;
    
    case PARENT_TO_CHILD_BOOTING:
        /// TODO: catch absolutely every exception here, end the coroutine, and return the exception
        child_status = RUNNING;
        
        // Invoke the child. We take the view that this is enough to give
        // it its first "timeslice"
        child_function(this);
        
        // If we get here, child returned without yielding (i.e. like a normal function).
        child_status = COMPLETE;
        longjmp(child_jmp_buf, CHILD_TO_PARENT);
        // No break required: longjump does not return
            
    default:
        // This setjmp call was only to get the stack pointer. 
        throw GCoroutine_bad_longjmp(__FILE__, __LINE__, val);
    }
}


GCoroutine::~GCoroutine()
{
    if( child_status != COMPLETE )
        throw GCoroutine_TODO("cancel child by injecting an exception");
    delete[] child_stack_memory;
}

        
void GCoroutine::run_iteration()
{
    jmp_buf parent_jmp_buf;
    int val;
    switch( val = setjmp(parent_jmp_buf) )
    {                    
    case IMMEDIATE:
        switch( child_status )
        {
        case READY:
            longjmp(child_jmp_buf, PARENT_TO_CHILD_BOOTING);
            // No break required: longjump does not return

        case RUNNING:
            // @TODO Store message if required, (queues will be an add-on)
            longjmp(child_jmp_buf, PARENT_TO_CHILD);
            // No break required: longjump does not return
        
        case COMPLETE:
            return; // TODO get the stored completed message
        }   
    case CHILD_TO_PARENT:
        return; // TODO child's stored waiting or completed message
        
    default:
        throw GCoroutine_bad_longjmp(__FILE__, __LINE__, val);
    }
}        
        
        
// @TODO use r9 to always have a GCoroutine::this ready to go
void GCoroutine::yield()
{
    // @TODO check we're in the correct stack. If not then (a) we're the
    // wrong child or (b) we overflowed or underflowed. Using r9 to track 
    // current child could prevent (a) and guard/fence zones could detect (b)
    // Think on...
    switch( val = setjmp(child_jmp_buf) )
    {                    
    case IMMEDIATE:
        // Run the main routine
        longjmp(child_jmp_buf, CHILD_TO_PARENT);
        // No break required: longjump does not return
        
    case PARENT_TO_CHILD:
        // If the child has ever yielded, it's context will come back to here
        // @TODO throw if required
        return; 
        
    default:
        throw GCoroutine_bad_longjmp(__FILE__, __LINE__, val);
    }    
}

