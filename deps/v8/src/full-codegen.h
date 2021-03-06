// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_FULL_CODEGEN_H_
#define V8_FULL_CODEGEN_H_

#include "v8.h"

#include "ast.h"
#include "compiler.h"

namespace v8 {
namespace internal {

// AST node visitor which can tell whether a given statement will be breakable
// when the code is compiled by the full compiler in the debugger. This means
// that there will be an IC (load/store/call) in the code generated for the
// debugger to piggybag on.
class BreakableStatementChecker: public AstVisitor {
 public:
  BreakableStatementChecker() : is_breakable_(false) {}

  void Check(Statement* stmt);
  void Check(Expression* stmt);

  bool is_breakable() { return is_breakable_; }

 private:
  // AST node visit functions.
#define DECLARE_VISIT(type) virtual void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

  bool is_breakable_;

  DISALLOW_COPY_AND_ASSIGN(BreakableStatementChecker);
};


// -----------------------------------------------------------------------------
// Full code generator.

class FullCodeGenerator: public AstVisitor {
 public:
  explicit FullCodeGenerator(MacroAssembler* masm)
      : masm_(masm),
        info_(NULL),
        nesting_stack_(NULL),
        loop_depth_(0),
        location_(kStack),
        true_label_(NULL),
        false_label_(NULL),
        fall_through_(NULL) {
  }

  static Handle<Code> MakeCode(CompilationInfo* info);

  void Generate(CompilationInfo* info);

 private:
  class Breakable;
  class Iteration;
  class TryCatch;
  class TryFinally;
  class Finally;
  class ForIn;

  class NestedStatement BASE_EMBEDDED {
   public:
    explicit NestedStatement(FullCodeGenerator* codegen) : codegen_(codegen) {
      // Link into codegen's nesting stack.
      previous_ = codegen->nesting_stack_;
      codegen->nesting_stack_ = this;
    }
    virtual ~NestedStatement() {
      // Unlink from codegen's nesting stack.
      ASSERT_EQ(this, codegen_->nesting_stack_);
      codegen_->nesting_stack_ = previous_;
    }

    virtual Breakable* AsBreakable() { return NULL; }
    virtual Iteration* AsIteration() { return NULL; }
    virtual TryCatch* AsTryCatch() { return NULL; }
    virtual TryFinally* AsTryFinally() { return NULL; }
    virtual Finally* AsFinally() { return NULL; }
    virtual ForIn* AsForIn() { return NULL; }

    virtual bool IsContinueTarget(Statement* target) { return false; }
    virtual bool IsBreakTarget(Statement* target) { return false; }

    // Generate code to leave the nested statement. This includes
    // cleaning up any stack elements in use and restoring the
    // stack to the expectations of the surrounding statements.
    // Takes a number of stack elements currently on top of the
    // nested statement's stack, and returns a number of stack
    // elements left on top of the surrounding statement's stack.
    // The generated code must preserve the result register (which
    // contains the value in case of a return).
    virtual int Exit(int stack_depth) {
      // Default implementation for the case where there is
      // nothing to clean up.
      return stack_depth;
    }
    NestedStatement* outer() { return previous_; }
   protected:
    MacroAssembler* masm() { return codegen_->masm(); }
   private:
    FullCodeGenerator* codegen_;
    NestedStatement* previous_;
    DISALLOW_COPY_AND_ASSIGN(NestedStatement);
  };

  class Breakable : public NestedStatement {
   public:
    Breakable(FullCodeGenerator* codegen,
              BreakableStatement* break_target)
        : NestedStatement(codegen),
          target_(break_target) {}
    virtual ~Breakable() {}
    virtual Breakable* AsBreakable() { return this; }
    virtual bool IsBreakTarget(Statement* statement) {
      return target_ == statement;
    }
    BreakableStatement* statement() { return target_; }
    Label* break_target() { return &break_target_label_; }
   private:
    BreakableStatement* target_;
    Label break_target_label_;
    DISALLOW_COPY_AND_ASSIGN(Breakable);
  };

  class Iteration : public Breakable {
   public:
    Iteration(FullCodeGenerator* codegen,
              IterationStatement* iteration_statement)
        : Breakable(codegen, iteration_statement) {}
    virtual ~Iteration() {}
    virtual Iteration* AsIteration() { return this; }
    virtual bool IsContinueTarget(Statement* statement) {
      return this->statement() == statement;
    }
    Label* continue_target() { return &continue_target_label_; }
   private:
    Label continue_target_label_;
    DISALLOW_COPY_AND_ASSIGN(Iteration);
  };

  // The environment inside the try block of a try/catch statement.
  class TryCatch : public NestedStatement {
   public:
    explicit TryCatch(FullCodeGenerator* codegen, Label* catch_entry)
        : NestedStatement(codegen), catch_entry_(catch_entry) { }
    virtual ~TryCatch() {}
    virtual TryCatch* AsTryCatch() { return this; }
    Label* catch_entry() { return catch_entry_; }
    virtual int Exit(int stack_depth);
   private:
    Label* catch_entry_;
    DISALLOW_COPY_AND_ASSIGN(TryCatch);
  };

  // The environment inside the try block of a try/finally statement.
  class TryFinally : public NestedStatement {
   public:
    explicit TryFinally(FullCodeGenerator* codegen, Label* finally_entry)
        : NestedStatement(codegen), finally_entry_(finally_entry) { }
    virtual ~TryFinally() {}
    virtual TryFinally* AsTryFinally() { return this; }
    Label* finally_entry() { return finally_entry_; }
    virtual int Exit(int stack_depth);
   private:
    Label* finally_entry_;
    DISALLOW_COPY_AND_ASSIGN(TryFinally);
  };

  // A FinallyEnvironment represents being inside a finally block.
  // Abnormal termination of the finally block needs to clean up
  // the block's parameters from the stack.
  class Finally : public NestedStatement {
   public:
    explicit Finally(FullCodeGenerator* codegen) : NestedStatement(codegen) { }
    virtual ~Finally() {}
    virtual Finally* AsFinally() { return this; }
    virtual int Exit(int stack_depth) {
      return stack_depth + kFinallyStackElementCount;
    }
   private:
    // Number of extra stack slots occupied during a finally block.
    static const int kFinallyStackElementCount = 2;
    DISALLOW_COPY_AND_ASSIGN(Finally);
  };

  // A ForInEnvironment represents being inside a for-in loop.
  // Abnormal termination of the for-in block needs to clean up
  // the block's temporary storage from the stack.
  class ForIn : public Iteration {
   public:
    ForIn(FullCodeGenerator* codegen,
          ForInStatement* statement)
        : Iteration(codegen, statement) { }
    virtual ~ForIn() {}
    virtual ForIn* AsForIn() { return this; }
    virtual int Exit(int stack_depth) {
      return stack_depth + kForInStackElementCount;
    }
   private:
    static const int kForInStackElementCount = 5;
    DISALLOW_COPY_AND_ASSIGN(ForIn);
  };

  enum Location {
    kAccumulator,
    kStack
  };

  enum ConstantOperand {
    kNoConstants,
    kLeftConstant,
    kRightConstant
  };

  // Type of a member function that generates inline code for a native function.
  typedef void (FullCodeGenerator::*InlineFunctionGenerator)
      (ZoneList<Expression*>*);

  static const InlineFunctionGenerator kInlineFunctionGenerators[];

  // Compute the frame pointer relative offset for a given local or
  // parameter slot.
  int SlotOffset(Slot* slot);

  // Determine whether or not to inline the smi case for the given
  // operation.
  bool ShouldInlineSmiCase(Token::Value op);

  // Compute which (if any) of the operands is a compile-time constant.
  ConstantOperand GetConstantOperand(Token::Value op,
                                     Expression* left,
                                     Expression* right);

  // Emit code to convert a pure value (in a register, slot, as a literal,
  // or on top of the stack) into the result expected according to an
  // expression context.
  void Apply(Expression::Context context, Register reg);

  // Slot cannot have type Slot::LOOKUP.
  void Apply(Expression::Context context, Slot* slot);

  void Apply(Expression::Context context, Literal* lit);
  void ApplyTOS(Expression::Context context);

  // Emit code to discard count elements from the top of stack, then convert
  // a pure value into the result expected according to an expression
  // context.
  void DropAndApply(int count, Expression::Context context, Register reg);

  // Set up branch labels for a test expression.
  void PrepareTest(Label* materialize_true,
                   Label* materialize_false,
                   Label** if_true,
                   Label** if_false,
                   Label** fall_through);

  // Emit code to convert pure control flow to a pair of labels into the
  // result expected according to an expression context.
  void Apply(Expression::Context context,
             Label* materialize_true,
             Label* materialize_false);

  // Emit code to convert constant control flow (true or false) into
  // the result expected according to an expression context.
  void Apply(Expression::Context context, bool flag);

  // Helper function to convert a pure value into a test context.  The value
  // is expected on the stack or the accumulator, depending on the platform.
  // See the platform-specific implementation for details.
  void DoTest(Label* if_true, Label* if_false, Label* fall_through);

  // Helper function to split control flow and avoid a branch to the
  // fall-through label if it is set up.
  void Split(Condition cc,
             Label* if_true,
             Label* if_false,
             Label* fall_through);

  void Move(Slot* dst, Register source, Register scratch1, Register scratch2);
  void Move(Register dst, Slot* source);

  // Return an operand used to read/write to a known (ie, non-LOOKUP) slot.
  // May emit code to traverse the context chain, destroying the scratch
  // register.
  MemOperand EmitSlotSearch(Slot* slot, Register scratch);

  void VisitForEffect(Expression* expr) {
    Expression::Context saved_context = context_;
    context_ = Expression::kEffect;
    Visit(expr);
    context_ = saved_context;
  }

  void VisitForValue(Expression* expr, Location where) {
    Expression::Context saved_context = context_;
    Location saved_location = location_;
    context_ = Expression::kValue;
    location_ = where;
    Visit(expr);
    context_ = saved_context;
    location_ = saved_location;
  }

  void VisitForControl(Expression* expr,
                       Label* if_true,
                       Label* if_false,
                       Label* fall_through) {
    Expression::Context saved_context = context_;
    Label* saved_true = true_label_;
    Label* saved_false = false_label_;
    Label* saved_fall_through = fall_through_;
    context_ = Expression::kTest;
    true_label_ = if_true;
    false_label_ = if_false;
    fall_through_ = fall_through;
    Visit(expr);
    context_ = saved_context;
    true_label_ = saved_true;
    false_label_ = saved_false;
    fall_through_ = saved_fall_through;
  }

  void VisitDeclarations(ZoneList<Declaration*>* declarations);
  void DeclareGlobals(Handle<FixedArray> pairs);

  // Try to perform a comparison as a fast inlined literal compare if
  // the operands allow it.  Returns true if the compare operations
  // has been matched and all code generated; false otherwise.
  bool TryLiteralCompare(Token::Value op,
                         Expression* left,
                         Expression* right,
                         Label* if_true,
                         Label* if_false,
                         Label* fall_through);

  // Platform-specific code for a variable, constant, or function
  // declaration.  Functions have an initial value.
  void EmitDeclaration(Variable* variable,
                       Variable::Mode mode,
                       FunctionLiteral* function);

  // Platform-specific return sequence
  void EmitReturnSequence();

  // Platform-specific code sequences for calls
  void EmitCallWithStub(Call* expr);
  void EmitCallWithIC(Call* expr, Handle<Object> name, RelocInfo::Mode mode);
  void EmitKeyedCallWithIC(Call* expr, Expression* key, RelocInfo::Mode mode);

  // Platform-specific code for inline runtime calls.
  InlineFunctionGenerator FindInlineFunctionGenerator(Runtime::FunctionId id);

  void EmitInlineRuntimeCall(CallRuntime* expr);

#define EMIT_INLINE_RUNTIME_CALL(name, x, y) \
  void Emit##name(ZoneList<Expression*>* arguments);
  INLINE_FUNCTION_LIST(EMIT_INLINE_RUNTIME_CALL)
  INLINE_RUNTIME_FUNCTION_LIST(EMIT_INLINE_RUNTIME_CALL)
#undef EMIT_INLINE_RUNTIME_CALL

  // Platform-specific code for loading variables.
  void EmitLoadGlobalSlotCheckExtensions(Slot* slot,
                                         TypeofState typeof_state,
                                         Label* slow);
  MemOperand ContextSlotOperandCheckExtensions(Slot* slot, Label* slow);
  void EmitDynamicLoadFromSlotFastCase(Slot* slot,
                                       TypeofState typeof_state,
                                       Label* slow,
                                       Label* done);
  void EmitVariableLoad(Variable* expr, Expression::Context context);

  // Platform-specific support for allocating a new closure based on
  // the given function info.
  void EmitNewClosure(Handle<SharedFunctionInfo> info);

  // Platform-specific support for compiling assignments.

  // Load a value from a named property.
  // The receiver is left on the stack by the IC.
  void EmitNamedPropertyLoad(Property* expr);

  // Load a value from a keyed property.
  // The receiver and the key is left on the stack by the IC.
  void EmitKeyedPropertyLoad(Property* expr);

  // Apply the compound assignment operator. Expects the left operand on top
  // of the stack and the right one in the accumulator.
  void EmitBinaryOp(Token::Value op,
                    Expression::Context context,
                    OverwriteMode mode);

  // Helper functions for generating inlined smi code for certain
  // binary operations.
  void EmitInlineSmiBinaryOp(Expression* expr,
                             Token::Value op,
                             Expression::Context context,
                             OverwriteMode mode,
                             Expression* left,
                             Expression* right,
                             ConstantOperand constant);

  void EmitConstantSmiBinaryOp(Expression* expr,
                               Token::Value op,
                               Expression::Context context,
                               OverwriteMode mode,
                               bool left_is_constant_smi,
                               Smi* value);

  void EmitConstantSmiBitOp(Expression* expr,
                            Token::Value op,
                            Expression::Context context,
                            OverwriteMode mode,
                            Smi* value);

  void EmitConstantSmiShiftOp(Expression* expr,
                              Token::Value op,
                              Expression::Context context,
                              OverwriteMode mode,
                              Smi* value);

  void EmitConstantSmiAdd(Expression* expr,
                          Expression::Context context,
                          OverwriteMode mode,
                          bool left_is_constant_smi,
                          Smi* value);

  void EmitConstantSmiSub(Expression* expr,
                          Expression::Context context,
                          OverwriteMode mode,
                          bool left_is_constant_smi,
                          Smi* value);

  // Assign to the given expression as if via '='. The right-hand-side value
  // is expected in the accumulator.
  void EmitAssignment(Expression* expr);

  // Complete a variable assignment.  The right-hand-side value is expected
  // in the accumulator.
  void EmitVariableAssignment(Variable* var,
                              Token::Value op,
                              Expression::Context context);

  // Complete a named property assignment.  The receiver is expected on top
  // of the stack and the right-hand-side value in the accumulator.
  void EmitNamedPropertyAssignment(Assignment* expr);

  // Complete a keyed property assignment.  The receiver and key are
  // expected on top of the stack and the right-hand-side value in the
  // accumulator.
  void EmitKeyedPropertyAssignment(Assignment* expr);

  void SetFunctionPosition(FunctionLiteral* fun);
  void SetReturnPosition(FunctionLiteral* fun);
  void SetStatementPosition(Statement* stmt);
  void SetExpressionPosition(Expression* expr, int pos);
  void SetStatementPosition(int pos);
  void SetSourcePosition(int pos);

  // Non-local control flow support.
  void EnterFinallyBlock();
  void ExitFinallyBlock();

  // Loop nesting counter.
  int loop_depth() { return loop_depth_; }
  void increment_loop_depth() { loop_depth_++; }
  void decrement_loop_depth() {
    ASSERT(loop_depth_ > 0);
    loop_depth_--;
  }

  MacroAssembler* masm() { return masm_; }

  Handle<Script> script() { return info_->script(); }
  bool is_eval() { return info_->is_eval(); }
  FunctionLiteral* function() { return info_->function(); }
  Scope* scope() { return info_->scope(); }

  static Register result_register();
  static Register context_register();

  // Set fields in the stack frame. Offsets are the frame pointer relative
  // offsets defined in, e.g., StandardFrameConstants.
  void StoreToFrameField(int frame_offset, Register value);

  // Load a value from the current context. Indices are defined as an enum
  // in v8::internal::Context.
  void LoadContextField(Register dst, int context_index);

  // Create an operand for a context field.
  MemOperand ContextOperand(Register context, int context_index);

  // AST node visit functions.
#define DECLARE_VISIT(type) virtual void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT
  // Handles the shortcutted logical binary operations in VisitBinaryOperation.
  void EmitLogicalOperation(BinaryOperation* expr);

  void VisitForTypeofValue(Expression* expr, Location where);

  void VisitLogicalForValue(Expression* expr,
                            Token::Value op,
                            Location where,
                            Label* done);


  MacroAssembler* masm_;
  CompilationInfo* info_;

  Label return_label_;
  NestedStatement* nesting_stack_;
  int loop_depth_;

  Expression::Context context_;
  Location location_;
  Label* true_label_;
  Label* false_label_;
  Label* fall_through_;

  friend class NestedStatement;

  DISALLOW_COPY_AND_ASSIGN(FullCodeGenerator);
};


} }  // namespace v8::internal

#endif  // V8_FULL_CODEGEN_H_
