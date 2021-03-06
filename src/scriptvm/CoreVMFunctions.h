/*
 * Copyright (c) 2014-2015 Christian Schoenebeck
 *
 * http://www.linuxsampler.org
 *
 * This file is part of LinuxSampler and released under the same terms.
 * See README file for details.
 */

#ifndef LS_COREVMFUNCTIONS_H
#define LS_COREVMFUNCTIONS_H

#include "../common/global.h"
#include "common.h"

namespace LinuxSampler {
    
class ScriptVM;

///////////////////////////////////////////////////////////////////////////
// convenience base classes for built-in script functions ...

/**
 * An instance of this class is returned by built-in function implementations
 * which do not return a function return value.
 */
class VMEmptyResult : public VMFnResult, public VMExpr {
public:
    StmtFlags_t flags; ///< general completion status (i.e. success or failure) of the function call

    VMEmptyResult() : flags(STMT_SUCCESS) {}
    ExprType_t exprType() const { return EMPTY_EXPR; }
    VMExpr* resultValue() { return this; }
    StmtFlags_t resultFlags() { return flags; }
};

/**
 * An instance of this class is returned by built-in function implementations
 * which return an integer value as function return value.
 */
class VMIntResult : public VMFnResult, public VMIntExpr {
public:
    StmtFlags_t flags; ///< general completion status (i.e. success or failure) of the function call
    int value; ///< result value of the function call

    VMIntResult() : flags(STMT_SUCCESS) {}
    int evalInt() { return value; }
    VMExpr* resultValue() { return this; }
    StmtFlags_t resultFlags() { return flags; }
};

/**
 * An instance of this class is returned by built-in function implementations
 * which return a string value as function return value.
 */
class VMStringResult : public VMFnResult, public VMStringExpr {
public:
    StmtFlags_t flags; ///< general completion status (i.e. success or failure) of the function call
    String value; ///< result value of the function call

    VMStringResult() : flags(STMT_SUCCESS) {}
    String evalStr() { return value; }
    VMExpr* resultValue() { return this; }
    StmtFlags_t resultFlags() { return flags; }
};

/**
 * Abstract base class for built-in script functions which do not return any
 * function return value (void).
 */
class VMEmptyResultFunction : public VMFunction {
protected:
    ExprType_t returnType() { return EMPTY_EXPR; }
    VMFnResult* errorResult();
    VMFnResult* successResult();
protected:
    VMEmptyResult result;
};

/**
 * Abstract base class for built-in script functions which return an integer
 * (scalar) as their function return value.
 */
class VMIntResultFunction : public VMFunction {
protected:
    ExprType_t returnType() { return INT_EXPR; }
    VMFnResult* errorResult(int i = 0);
    VMFnResult* successResult(int i = 0);
protected:
    VMIntResult result;
};

/**
 * Abstract base class for built-in script functions which return a string as
 * their function return value.
 */
class VMStringResultFunction : public VMFunction {
protected:
    ExprType_t returnType() { return STRING_EXPR; }
    VMFnResult* errorResult(const String& s = "");
    VMFnResult* successResult(const String& s = "");
protected:
    VMStringResult result;
};


///////////////////////////////////////////////////////////////////////////
// implementations of core built-in script functions ...

/**
 * Implements the built-in message() script function.
 */
class CoreVMFunction_message : public VMEmptyResultFunction {
public:
    int minRequiredArgs() const { return 1; }
    int maxAllowedArgs() const { return 1; }
    bool acceptsArgType(int iArg, ExprType_t type) const;
    ExprType_t argType(int iArg) const { return STRING_EXPR; }
    VMFnResult* exec(VMFnArgs* args);
};

/**
 * Implements the built-in exit() script function.
 */
class CoreVMFunction_exit : public VMEmptyResultFunction {
public:
    int minRequiredArgs() const { return 0; }
    int maxAllowedArgs() const { return 0; }
    bool acceptsArgType(int iArg, ExprType_t type) const { return false; }
    ExprType_t argType(int iArg) const { return INT_EXPR; /*whatever*/ }
    VMFnResult* exec(VMFnArgs* args);
};

/**
 * Implements the built-in wait() script function.
 */
class CoreVMFunction_wait : public VMEmptyResultFunction {
public:
    CoreVMFunction_wait(ScriptVM* vm) : vm(vm) {}
    int minRequiredArgs() const { return 1; }
    int maxAllowedArgs() const { return 1; }
    bool acceptsArgType(int iArg, ExprType_t type) const { return type == INT_EXPR; }
    ExprType_t argType(int iArg) const { return INT_EXPR; }
    VMFnResult* exec(VMFnArgs* args);
protected:
    ScriptVM* vm;
};

/**
 * Implements the built-in abs() script function.
 */
class CoreVMFunction_abs : public VMIntResultFunction {
public:
    int minRequiredArgs() const { return 1; }
    int maxAllowedArgs() const { return 1; }
    bool acceptsArgType(int iArg, ExprType_t type) const;
    ExprType_t argType(int iArg) const { return INT_EXPR; }
    VMFnResult* exec(VMFnArgs* args);
};

/**
 * Implements the built-in random() script function.
 */
class CoreVMFunction_random : public VMIntResultFunction {
public:
    int minRequiredArgs() const { return 2; }
    int maxAllowedArgs() const { return 2; }
    bool acceptsArgType(int iArg, ExprType_t type) const;
    ExprType_t argType(int iArg) const { return INT_EXPR; }
    VMFnResult* exec(VMFnArgs* args);
};

/**
 * Implements the built-in num_elements() script function.
 */
class CoreVMFunction_num_elements : public VMIntResultFunction {
public:
    int minRequiredArgs() const { return 1; }
    int maxAllowedArgs() const { return 1; }
    bool acceptsArgType(int iArg, ExprType_t type) const;
    ExprType_t argType(int iArg) const { return INT_ARR_EXPR; }
    VMFnResult* exec(VMFnArgs* args);
};

} // namespace LinuxSampler

#endif // LS_COREVMFUNCTIONS_H
