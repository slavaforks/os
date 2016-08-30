/*++

Copyright (c) 2016 Minoca Corp. All Rights Reserved

Module Name:

    capi.c

Abstract:

    This module implement the Chalk C API, which allows Chalk and C to
    interface naturally together.

Author:

    Evan Green 14-Aug-2016

Environment:

    C

--*/

//
// ------------------------------------------------------------------- Includes
//

#include "chalkp.h"

//
// --------------------------------------------------------------------- Macros
//

//
// These macros evaluate to non-zero if the stack has room for a push or a pop
// of the given size.
//

#define CK_CAN_PUSH(_Fiber, _Count) \
    ((_Fiber)->StackTop + (_Count) <= \
     ((_Fiber)->Stack + (_Fiber)->StackCapacity))

#define CK_CAN_POP(_Fiber, _Count) \
    ((_Fiber)->StackTop - (_Count) >= (_Fiber)->Stack)

//
// ---------------------------------------------------------------- Definitions
//

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

PCK_VALUE
CkpGetStackIndex (
    PCK_VM Vm,
    INTN Index
    );

//
// -------------------------------------------------------------------- Globals
//

//
// Define the mapping between built-in object type and API types.
//

const CK_API_TYPE CkApiObjectTypes[CkObjectTypeCount] = {
    CkTypeInvalid,  // CkObjectInvalid
    CkTypeObject,   // CkObjectClass
    CkTypeFunction, // CkObjectClosure
    CkTypeDict,     // CkObjectDict
    CkTypeObject,   // CkObjectFiber
    CkTypeObject,   // CkObjectForeign
    CkTypeObject,   // CkObjectFunction
    CkTypeObject,   // CkObjectInstance
    CkTypeList,     // CkObjectList
    CkTypeObject,   // CkObjectModule
    CkTypeObject,   // CkObjectRange
    CkTypeString,   // CkObjectString
    CkTypeObject,   // CkObjectUpvalue
};

//
// ------------------------------------------------------------------ Functions
//

CK_API
BOOL
CkPreloadForeignModule (
    PCK_VM Vm,
    PSTR ModuleName,
    PSTR Path,
    PVOID Handle,
    PCK_FOREIGN_FUNCTION LoadFunction
    )

/*++

Routine Description:

    This routine registers the availability of a foreign module that might not
    otherwise be reachable via the standard module load methods. This is often
    used for adding specialized modules in an embedded interpreter. The load
    function isn't called until someone actually imports the module from the
    interpreter. The loaded module is pushed onto the stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    ModuleName - Supplies a pointer to the full "dotted.module.name". A copy of
        this memory will be made.

    Path - Supplies an optional pointer to the full path of the module. A copy
        of this memory will be made.

    Handle - Supplies an optional pointer to a handle (usually a dynamic
        library handle) that is used if the module is unloaded.

    LoadFunction - Supplies a pointer to a C function to call to load the
        module symbols. The function will be called on a new fiber, with the
        module itself in slot zero.

Return Value:

    TRUE on success.

    FALSE on failure (usually allocation failure).

--*/

{

    PCK_FIBER Fiber;
    PCK_MODULE Module;
    CK_VALUE ModuleValue;
    CK_VALUE NameString;
    CK_VALUE PathString;

    NameString = CkpStringCreate(Vm, ModuleName, strlen(ModuleName));
    if (CK_IS_NULL(NameString)) {
        return FALSE;
    }

    PathString = CkNullValue;
    if (Path != NULL) {
        CkpPushRoot(Vm, CK_AS_OBJECT(NameString));
        PathString = CkpStringCreate(Vm, Path, strlen(Path));
        CkpPopRoot(Vm);
        if (CK_IS_NULL(PathString)) {
            return FALSE;
        }
    }

    Module = CkpModuleLoadForeign(Vm,
                                  NameString,
                                  PathString,
                                  Handle,
                                  LoadFunction);

    if (Module == NULL) {
        return FALSE;
    }

    CK_OBJECT_VALUE(ModuleValue, Module);
    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    CK_PUSH(Vm->Fiber, ModuleValue);
    return TRUE;
}

CK_API
UINTN
CkGetStackSize (
    PCK_VM Vm
    )

/*++

Routine Description:

    This routine gets the current stack size.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

Return Value:

    Returns the number of stack slots available to the C API.

--*/

{

    PCK_FIBER Fiber;
    PCK_CALL_FRAME Frame;
    PCK_VALUE StackEnd;

    Fiber = Vm->Fiber;
    if (Fiber == NULL) {
        return 0;
    }

    //
    // If there's a call frame, return the number of slots available starting
    // at this frame.
    //

    if (Fiber->FrameCount != 0) {
        Frame = &(Fiber->Frames[Fiber->FrameCount - 1]);
        StackEnd = Fiber->Stack + Fiber->StackCapacity;

        CK_ASSERT(Frame->StackStart < StackEnd);

        return StackEnd - Frame->StackStart;
    }

    //
    // If there's no call frame, return the direct capacity.
    //

    return Fiber->StackCapacity;
}

CK_API
BOOL
CkEnsureStack (
    PCK_VM Vm,
    UINTN Size
    )

/*++

Routine Description:

    This routine ensures that there are at least the given number of
    stack slots currently available for the C API.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Size - Supplies the number of additional stack slots needed by the C API.

Return Value:

    TRUE on success.

    FALSE on allocation failure.

--*/

{

    PCK_FIBER Fiber;

    //
    // Initialize a fiber if needed.
    //

    if (Vm->Fiber == NULL) {
        Vm->Fiber = CkpFiberCreate(Vm, NULL);
        if (Vm->Fiber == NULL) {
            return FALSE;
        }
    }

    Fiber = Vm->Fiber;
    if (Fiber->StackTop + Size > Fiber->Stack + Fiber->StackCapacity) {
        CkpEnsureStack(Vm, Fiber, (Fiber->StackTop - Fiber->Stack) + Size);
        if (Fiber->StackTop + Size > Fiber->Stack + Fiber->StackCapacity) {
            return FALSE;
        }
    }

    return TRUE;
}

CK_API
VOID
CkPushValue (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine pushes a value already on the stack to the top of the stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the existing value to push.
        Negative values reference stack indices from the end of the stack.

Return Value:

    None.

--*/

{

    PCK_FIBER Fiber;
    PCK_VALUE Source;

    Source = CkpGetStackIndex(Vm, StackIndex);
    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    CK_PUSH(Vm->Fiber, *Source);
    return;
}

CK_API
VOID
CkStackRemove (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine removes a value from the stack, and shifts all the other
    values down.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the value to remove. Negative
        values reference stack indices from the end of the stack.

Return Value:

    None.

--*/

{

    PCK_FIBER Fiber;
    PCK_VALUE Source;

    Fiber = Vm->Fiber;
    Source = CkpGetStackIndex(Vm, StackIndex);
    while (Source + 1 < Fiber->StackTop) {
        *Source = *(Source + 1);
        Source += 1;
    }

    CK_ASSERT(CK_CAN_POP(Fiber, 1));

    Fiber->StackTop -= 1;
    return;
}

CK_API
VOID
CkStackInsert (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine adds the element at the top of the stack into the given
    stack position, and shifts all remaining elements over.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index location to insert at. Negative
        values reference stack indices from the end of the stack.

Return Value:

    None.

--*/

{

    PCK_VALUE Destination;
    PCK_FIBER Fiber;
    PCK_VALUE Move;

    Fiber = Vm->Fiber;
    Destination = CkpGetStackIndex(Vm, StackIndex);

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    Move = Fiber->StackTop;
    while (Move > Destination) {
        *Move = *(Move - 1);
        Move -= 1;
    }

    *Destination = *(Fiber->StackTop);
    Fiber->StackTop += 1;
    return;
}

CK_API
VOID
CkStackReplace (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine pops the value from the top of the stack and replaces the
    value at the given stack index with it.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index to replace with the top of the stack.
        Negative values reference stack indices from the end of the stack. This
        is the stack index before the value is popped.

Return Value:

    None.

--*/

{

    PCK_VALUE Destination;
    PCK_FIBER Fiber;

    Destination = CkpGetStackIndex(Vm, StackIndex);
    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_POP(Fiber, 1));

    *Destination = CK_POP(Fiber);
    return;
}

CK_API
CK_API_TYPE
CkGetType (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine returns the type of the value at the given stack index.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the object to query. Negative
        values reference stack indices from the end of the stack.

Return Value:

    Returns the stack type.

--*/

{

    PCK_OBJECT Object;
    PCK_VALUE Value;

    Value = CkpGetStackIndex(Vm, StackIndex);
    switch (Value->Type) {
    case CkValueNull:
        return CkTypeNull;

    case CkValueInteger:
        return CkTypeInteger;

    case CkValueObject:
        Object = CK_AS_OBJECT(*Value);

        CK_ASSERT(Object->Type < CkObjectTypeCount);

        return CkApiObjectTypes[Object->Type];

    default:

        CK_ASSERT(FALSE);

        break;
    }

    return CkTypeInvalid;
}

CK_API
VOID
CkPushNull (
    PCK_VM Vm
    )

/*++

Routine Description:

    This routine pushes a null value on the top of the stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

Return Value:

    None.

--*/

{

    PCK_FIBER Fiber;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    CK_PUSH(Fiber, CkNullValue);
    return;
}

CK_API
VOID
CkPushInteger (
    PCK_VM Vm,
    CK_INTEGER Integer
    )

/*++

Routine Description:

    This routine pushes an integer value on the top of the stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Integer - Supplies the integer to push.

Return Value:

    None.

--*/

{

    PCK_FIBER Fiber;
    CK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    CK_INT_VALUE(Value, Integer);
    CK_PUSH(Fiber, Value);
    return;
}

CK_API
CK_INTEGER
CkGetInteger (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine returns an integer at the given stack index.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the object to get. Negative
        values reference stack indices from the end of the stack.

Return Value:

    Returns the integer value.

    0 if the value at the stack is not an integer.

--*/

{

    PCK_VALUE Value;

    Value = CkpGetStackIndex(Vm, StackIndex);
    if (!CK_IS_INTEGER(*Value)) {
        return 0;
    }

    return CK_AS_INTEGER(*Value);
}

CK_API
VOID
CkPushString (
    PCK_VM Vm,
    PCSTR String,
    UINTN Length
    )

/*++

Routine Description:

    This routine pushes a string value on the top of the stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    String - Supplies a pointer to the string data to push. A copy of this
        string will be made.

    Length - Supplies the length of the string in bytes, not including the
        null terminator.

Return Value:

    None.

--*/

{

    PCK_FIBER Fiber;
    CK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    Value = CkpStringCreate(Vm, String, Length);
    CK_PUSH(Fiber, Value);
    return;
}

CK_API
PCSTR
CkGetString (
    PCK_VM Vm,
    UINTN StackIndex,
    PUINTN Length
    )

/*++

Routine Description:

    This routine returns a string at the given stack index.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the object to get. Negative
        values reference stack indices from the end of the stack.

    Length - Supplies an optional pointer where the length of the string will
        be returned, not including a null terminator. If the value at the stack
        index is not a string, 0 is returned here.

Return Value:

    Returns a pointer to the string. The caller must not modify or free this
    value.

    NULL if the value at the specified stack index is not a string.

--*/

{

    PCK_STRING String;
    PCK_VALUE Value;

    Value = CkpGetStackIndex(Vm, StackIndex);
    if (!CK_IS_STRING(*Value)) {
        if (Length != NULL) {
            *Length = 0;
        }

        return NULL;
    }

    String = CK_AS_STRING(*Value);
    if (Length != NULL) {
        *Length = String->Length;
    }

    return String->Value;
}

CK_API
VOID
CkPushSubstring (
    PCK_VM Vm,
    INTN StackIndex,
    INTN Start,
    INTN End
    )

/*++

Routine Description:

    This routine creates a new string consisting of a portion of the string
    at the given stack index, and pushes it on the stack. If the value at the
    given stack index is not a string, then an empty string is pushed as the
    result. If either the start or end indices are out of range, they are
    adjusted to be in range.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the string to slice. Negative
        values reference stack indices from the end of the stack.

    Start - Supplies the starting index of the substring, inclusive. Negative
        values reference from the end of the string, with -1 being after the
        last character of the string.

    End - Supplies the ending index of the substring, exclusive. Negative
        values reference from the end of the string, with -1 being after the
        last character of the string.

Return Value:

    None.

--*/

{

    UINTN EndIndex;
    PCK_FIBER Fiber;
    PCK_VALUE Source;
    UINTN StartIndex;
    PCK_STRING String;
    CK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    Source = CkpGetStackIndex(Vm, StackIndex);
    if (!CK_IS_STRING(*Source)) {
        CkPushString(Vm, "", 0);
        return;
    }

    String = CK_AS_STRING(*Source);

    //
    // Make the values in range.
    //

    if (Start > String->Length) {
        Start = String->Length;

    } else if (Start < -String->Length) {
        Start = 0;
    }

    if (End > String->Length) {
        End = String->Length;

    } else if (End < -String->Length) {
        End = 0;
    }

    //
    // Convert the indices (which might be negative) into positive indices.
    //

    CK_INT_VALUE(Value, Start);
    StartIndex = CkpGetIndex(Vm, Value, String->Length);
    CK_INT_VALUE(Value, End);
    EndIndex = CkpGetIndex(Vm, Value, String->Length);

    CK_ASSERT((StartIndex <= String->Length) && (EndIndex <= String->Length));

    //
    // If the indices cross each other or are beyond the string, just push the
    // empty string.
    //

    if ((StartIndex >= String->Length) || (StartIndex >= EndIndex)) {
        CkPushString(Vm, "", 0);

    //
    // Otherwise, create the substring.
    //

    } else {
        CkPushString(Vm, String->Value + StartIndex, EndIndex - StartIndex);
    }

    return;
}

CK_API
VOID
CkStringConcatenate (
    PCK_VM Vm,
    UINTN Count
    )

/*++

Routine Description:

    This routine pops a given number of strings off the stack and concatenates
    them. The resulting string is then pushed on the stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Count - Supplies the number of strings to pop off the stack.

Return Value:

    None.

--*/

{

    PSTR Destination;
    PCK_FIBER Fiber;
    UINTN Index;
    PCK_STRING NewString;
    CK_VALUE NewValue;
    UINTN Size;
    PCK_STRING Source;
    PCK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(Count != 0);
    CK_ASSERT(CK_CAN_POP(Fiber, Count - 1));

    Value = Fiber->StackTop - 1;

    //
    // Loop through once to get the size.
    //

    Size = 0;
    for (Index = 0; Index < Count; Index += 1) {
        if (CK_IS_STRING(*Value)) {
            Source = CK_AS_STRING(*Value);
            Size += Source->Length;
        }
    }

    NewString = CkpStringAllocate(Vm, Size);
    if (NewString == NULL) {
        Fiber->StackTop -= Count;
        CK_PUSH(Fiber, CkNullValue);
        return;
    }

    //
    // Loop through again to create the concatenated string.
    //

    Destination = NewString->Value;
    for (Index = 0; Index < Count; Index += 1) {
        if (CK_IS_STRING(*Value)) {
            Source = CK_AS_STRING(*Value);
            CkCopy(Destination, Source->Value, Source->Length);
            Destination += Source->Length;
        }
    }

    CkpStringHash(NewString);
    Fiber->StackTop -= Count;
    CK_OBJECT_VALUE(NewValue, NewString);
    CK_PUSH(Fiber, NewValue);
    return;
}

CK_API
VOID
CkPushDict (
    PCK_VM Vm
    )

/*++

Routine Description:

    This routine creates a new empty dictionary and pushes it onto the stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

Return Value:

    None.

--*/

{

    PCK_DICT Dict;
    PCK_FIBER Fiber;
    CK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    Dict = CkpDictCreate(Vm);
    if (Dict == NULL) {
        CK_PUSH(Fiber, CkNullValue);

    } else {
        CK_OBJECT_VALUE(Value, Dict);
        CK_PUSH(Fiber, Value);
    }

    return;
}

CK_API
VOID
CkDictGet (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine pops a key value off the stack, and uses it to get the
    corresponding value for the dictionary stored at the given stack index.
    The resulting value is pushed onto the stack. If no value exists for the
    given key, then null is pushed.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the dictionary (before the key is
        popped off). Negative values reference stack indices from the end of
        the stack.

Return Value:

    Returns the integer value.

    0 if the value at the stack is not an integer.

--*/

{

    PCK_VALUE DictValue;
    PCK_FIBER Fiber;
    CK_VALUE Key;
    CK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_POP(Fiber, 1));

    DictValue = CkpGetStackIndex(Vm, StackIndex);
    Key = CK_POP(Fiber);
    if (!CK_IS_DICT(*DictValue)) {
        CK_PUSH(Fiber, CkNullValue);
        return;
    }

    Value = CkpDictGet(CK_AS_DICT(*DictValue), Key);
    if (CK_IS_UNDEFINED(Value)) {
        Value = CkNullValue;
    }

    CK_PUSH(Fiber, Value);
    return;
}

CK_API
VOID
CkDictSet (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine pops a key and then a value off the stack, then sets that
    key-value pair in the dictionary at the given stack index.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the dictionary (before anything is
        popped off). Negative values reference stack indices from the end of
        the stack.

Return Value:

    None.

--*/

{

    PCK_VALUE DictValue;
    PCK_FIBER Fiber;
    PCK_VALUE Key;
    PCK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_POP(Fiber, 1));

    DictValue = CkpGetStackIndex(Vm, StackIndex);
    Key = Fiber->StackTop - 1;
    Value = Fiber->StackTop - 2;
    if (CK_IS_DICT(*DictValue)) {
        CkpDictSet(Vm, CK_AS_DICT(*DictValue), *Key, *Value);
    }

    Fiber->StackTop -= 2;
    return;
}

CK_API
UINTN
CkDictSize (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine returns the size of the dictionary at the given stack index.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the dictionary. Negative values
        reference stack indices from the end of the stack.

Return Value:

    Returns the number of elements in the dictionary.

    0 if the list is empty or the referenced item is not a dictionary.

--*/

{

    PCK_DICT Dict;
    PCK_VALUE Value;

    Value = CkpGetStackIndex(Vm, StackIndex);
    if (!CK_IS_DICT(*Value)) {
        return 0;
    }

    Dict = CK_AS_DICT(*Value);
    return Dict->Count;
}

CK_API
BOOL
CkDictIterate (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine advances a dictionary iterator at the top of the stack. It
    pushes the next key and then the next value onto the stack, if there are
    more elements in the dictionary. Callers should pull a null values onto
    the stack as the initial iterator before calling this routine for the first
    time. Callers are responsible for popping the value, key, and potentially
    finished iterator off the stack. Callers should not modify a dictionary
    during iteration, as the results are undefined.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the dictionary. Negative values
        reference stack indices from the end of the stack.

Return Value:

    TRUE if the next key and value were pushed on.

    FALSE if there are no more elements, the iterator value is invalid, or the
    item at the given stack index is not a dictionary.

--*/

{

    PCK_DICT Dict;
    PCK_FIBER Fiber;
    CK_INTEGER Index;
    PCK_VALUE Iterator;
    PCK_VALUE Value;

    Fiber = Vm->Fiber;
    Value = CkpGetStackIndex(Vm, StackIndex);
    if (!CK_IS_DICT(*Value)) {
        return FALSE;
    }

    Dict = CK_AS_DICT(*Value);

    CK_ASSERT((CK_CAN_PUSH(Fiber, 2)) && (CK_CAN_POP(Fiber, 1)));

    Iterator = Fiber->StackTop - 1;
    Index = 0;
    if (!CK_IS_NULL(*Iterator)) {
        if (!CK_IS_INTEGER(*Iterator)) {
            return FALSE;
        }

        Index = CK_AS_INTEGER(*Iterator);
        if ((Index < 0) || (Index >= Dict->Capacity)) {
            *Iterator = CkNullValue;
            return FALSE;
        }

        Index += 1;
    }

    //
    // Find an occupied slot.
    //

    while (Index < Dict->Capacity) {
        if (!CK_IS_UNDEFINED(Dict->Entries[Index].Key)) {
            CK_INT_VALUE(*Iterator, Index);
            CK_PUSH(Fiber, Dict->Entries[Index].Key);
            CK_PUSH(Fiber, Dict->Entries[Index].Value);
            return TRUE;
        }

        Index += 1;
    }

    *Iterator = CkNullValue;
    return FALSE;
}

CK_API
VOID
CkPushList (
    PCK_VM Vm
    )

/*++

Routine Description:

    This routine creates a new empty list and pushes it onto the stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

Return Value:

    None.

--*/

{

    PCK_FIBER Fiber;
    PCK_LIST List;
    CK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    List = CkpListCreate(Vm, 0);
    if (List == NULL) {
        CK_PUSH(Fiber, CkNullValue);

    } else {
        CK_OBJECT_VALUE(Value, List);
        CK_PUSH(Fiber, Value);
    }

    return;
}

CK_API
VOID
CkListGet (
    PCK_VM Vm,
    INTN StackIndex,
    INTN ListIndex
    )

/*++

Routine Description:

    This routine gets the value at the given list index, and pushes it on the
    stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the list. Negative values
        reference stack indices from the end of the stack.

    ListIndex - Supplies the list index to get. If this index is out of bounds,
        the null will be pushed.

Return Value:

    None.

--*/

{

    PCK_FIBER Fiber;
    UINTN Index;
    CK_VALUE IndexValue;
    PCK_LIST List;
    PCK_VALUE ListValue;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    ListValue = CkpGetStackIndex(Vm, StackIndex);
    if (!CK_IS_LIST(*ListValue)) {
        CK_PUSH(Fiber, CkNullValue);
        return;
    }

    List = CK_AS_LIST(*ListValue);
    if ((ListIndex >= List->Elements.Count) ||
        (-ListIndex > List->Elements.Count)) {

        CK_PUSH(Fiber, CkNullValue);
        return;
    }

    CK_INT_VALUE(IndexValue, ListIndex);
    Index = CkpGetIndex(Vm, IndexValue, List->Elements.Count);

    CK_ASSERT(Index < List->Elements.Count);

    CK_PUSH(Fiber, List->Elements.Data[Index]);
    return;
}

CK_API
VOID
CkListSet (
    PCK_VM Vm,
    INTN StackIndex,
    INTN ListIndex
    )

/*++

Routine Description:

    This routine pops the top value off the stack, and saves it to a specific
    index in a list.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the list. Negative values
        reference stack indices from the end of the stack.

    ListIndex - Supplies the list index to set. If this index is one beyond the
        end, then the value will be appended. If this index is otherwise out of
        bounds, the item at the top of the stack will simply be discarded.

Return Value:

    None.

--*/

{

    PCK_FIBER Fiber;
    UINTN Index;
    CK_VALUE IndexValue;
    PCK_LIST List;
    PCK_VALUE ListValue;
    CK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_POP(Fiber, 1));

    ListValue = CkpGetStackIndex(Vm, StackIndex);
    if (!CK_IS_LIST(*ListValue)) {
        CK_PUSH(Fiber, CkNullValue);
        return;
    }

    List = CK_AS_LIST(*ListValue);
    Value = *(Fiber->StackTop - 1);
    if (ListIndex == List->Elements.Count) {
        CkpArrayAppend(Vm, &(List->Elements), Value);

    } else if ((ListIndex < List->Elements.Count) ||
               (-ListIndex <= List->Elements.Count)) {

        CK_INT_VALUE(IndexValue, ListIndex);
        Index = CkpGetIndex(Vm, IndexValue, List->Elements.Count);

        CK_ASSERT(Index < List->Elements.Count);

        List->Elements.Data[Index] = Value;
    }

    Fiber->StackTop -= 1;
    return;
}

CK_API
UINTN
CkListSize (
    PCK_VM Vm,
    INTN StackIndex
    )

/*++

Routine Description:

    This routine returns the size of the list at the given stack index.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    StackIndex - Supplies the stack index of the list. Negative values
        reference stack indices from the end of the stack.

Return Value:

    Returns the number of elements in the list.

    0 if the list is empty or the referenced item is not a list.

--*/

{

    PCK_LIST List;
    PCK_VALUE Value;

    Value = CkpGetStackIndex(Vm, StackIndex);
    if (!CK_IS_LIST(*Value)) {
        return 0;
    }

    List = CK_AS_LIST(*Value);
    return List->Elements.Count;
}

CK_API
VOID
CkPushModulePath (
    PCK_VM Vm
    )

/*++

Routine Description:

    This routine pushes the module path onto the stack.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

Return Value:

    None.

--*/

{

    PCK_FIBER Fiber;
    CK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(CK_CAN_PUSH(Fiber, 1));

    if (Vm->ModulePath == NULL) {
        Vm->ModulePath = CkpListCreate(Vm, 0);
        if (Vm->ModulePath == NULL) {
            CK_PUSH(Fiber, CkNullValue);
            return;
        }
    }

    CK_OBJECT_VALUE(Value, Vm->ModulePath);
    CK_PUSH(Fiber, Value);
    return;
}

//
// --------------------------------------------------------- Internal Functions
//

PCK_VALUE
CkpGetStackIndex (
    PCK_VM Vm,
    INTN Index
    )

/*++

Routine Description:

    This routine returns a pointer into the stack for the given stack index.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Index - Supplies the stack index to get. Negative values reference values
        from the end of the stack.

Return Value:

    Returns a pointer to the previous search path. The caller should not
    attempt to modify or free this value. It will be garbage collected.

--*/

{

    PCK_FIBER Fiber;
    PCK_VALUE Stack;
    PCK_VALUE Value;

    Fiber = Vm->Fiber;

    CK_ASSERT(Fiber != NULL);

    Stack = Fiber->Stack;
    if (Fiber->FrameCount != 0) {
        Stack = Fiber->Frames[Fiber->FrameCount - 1].StackStart;
    }

    if (Index >= 0) {
        Value = Stack + Index;

    } else {
        Value = Fiber->StackTop + Index;
    }

    CK_ASSERT((Value >= Stack) && (Value < Fiber->StackTop));

    return Value;
}
