#include "llvm/Passes/PassBuilder.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "ddlog.h"
#ifdef __cplusplus
}
#endif
#include "llvm/Config/abi-breaking.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

namespace {

class DDlogAnalyser {
 public:
  DDlogAnalyser() {
    Prog = ddlog_run(1, true, nullptr, nullptr);
    if (Prog == nullptr) {
      ddlogReportFatal("failed to initialize DDlog program");
    }
#ifdef DEBUG
    errs() << "DDlog start..\n";
#endif
    if (ddlog_transaction_start(Prog)) {
      ddlogReportFatal("failed to start transaction");
    }

    AllocInstTableID   = ddlog_get_table_id(Prog, AllocInst.c_str());
    LoadInstTableID    = ddlog_get_table_id(Prog, LoadInst.c_str());
    StoreInstTableID   = ddlog_get_table_id(Prog, StoreInst.c_str());
    VarPointsToTableID = ddlog_get_table_id(Prog, VarPointsTo.c_str());
  }

  ~DDlogAnalyser() {
    if (ddlog_stop(Prog)) {
      ddlogReportFatal("failed to stop DDlog program");
    }
#ifdef DEBUG
    errs() << "DDlog stop..\n";
#endif
  }

  void addAllocInst(const std::string &Var, const std::string &Obj) {
    insertCmdAndApplyUpdates(AllocInstTableID,
                             create2StringRecord(Var, Obj, AllocInst));
  }

  void addLoadInst(const std::string &Var, const std::string &Obj) {
    insertCmdAndApplyUpdates(LoadInstTableID,
                             create2StringRecord(Var, Obj, LoadInst));
  }

  void addStoreInst(const std::string &VarTarget,
                    const std::string &VarSource) {
    insertCmdAndApplyUpdates(
        StoreInstTableID, create2StringRecord(VarTarget, VarSource, StoreInst));
  }

  void commit() {
    if (ddlog_transaction_commit(Prog) < 0) {
      ddlogReportFatal("failed to commit transaction");
    }

    // print the result
    ddlog_dump_table(Prog, VarPointsToTableID, &ddlogPrintRecordCallBack,
                     (uintptr_t)(void *)(nullptr));
  }

 private:
  ddlog_prog Prog;
  table_id AllocInstTableID;
  table_id LoadInstTableID;
  table_id StoreInstTableID;
  table_id VarPointsToTableID;
  table_id FieldPointsToTableID;

  const std::string AllocInst     = "AllocInst";
  const std::string LoadInst      = "LoadInst";
  const std::string StoreInst     = "StoreInst";
  const std::string VarPointsTo   = "VarPointsTo";
  const std::string FieldPointsTo = "FieldPointsTo";

  void insertCmdAndApplyUpdates(table_id Table, ddlog_record *Record) {
    ddlog_cmd *Cmd = ddlog_insert_cmd(Table, Record);
    if (Cmd == nullptr) {
      ddlogReportFatal("failed to create insert command");
    }

    if (ddlog_apply_updates(Prog, &Cmd, 1) < 0) {
      ddlogReportFatal("failed to apply updates");
    }
  }

  ddlog_record *create2StringRecord(const std::string &X, const std::string &Y,
                                    const std::string &Constructor) {
    std::array<ddlog_record *, 2> Record;

    ddlog_record *XDDlog = ddlog_string(X.c_str());
    ddlog_record *YDDlog = ddlog_string(Y.c_str());

    Record[0] = XDDlog;
    Record[1] = YDDlog;

    ddlog_record *RecordDDlog =
        ddlog_struct(Constructor.c_str(), &Record.at(0), 2);

#ifdef DEBUG
    char *RecordToInsertAsString = ddlog_dump_record(RecordDDlog);
    printf("Inserting the following record: %s\n", RecordToInsertAsString);
    ddlog_string_free(RecordToInsertAsString);
#endif

    return RecordDDlog;
  }

  static bool ddlogPrintRecordCallBack(uintptr_t Arg, const ddlog_record *Rec,
                                       ssize_t Weight) {
    char *RecordAsString = ddlog_dump_record(Rec);
    if (RecordAsString == nullptr) {
      ddlogReportFatal("failed to dump record");
    }

    const char *Action = (Weight == 1) ? "Inserted" : "Deleted";
    errs() << Action << " record " << RecordAsString << "\n";
    ddlog_string_free(RecordAsString);

    return true;
  }

  static void ddlogReportFatal(const char *Str) {
    errs() << "[-] DDlog: " << Str << "\n";
    exit(EXIT_FAILURE);
  }
};

class ObjectMaker {
 public:
  static const std::string makeObject(Type *AllocatedType) {
    std::string TypeStr;
    llvm::raw_string_ostream RSO(TypeStr);
    AllocatedType->print(RSO);
    return RSO.str();
  }

  static const std::string makeVariable(Instruction &I) {
    auto *F = I.getFunction();

    if (!I.hasName() && !I.getType()->isVoidTy()) {
      I.setName("tmp");
    }

    return makeVariable(F, I.getName());
  }

  static const std::string makeVariableTarget(StoreInst &SI) {
    return SI.getOperand(1)->getName().str();
  }

  static const std::string makeVariableSource(StoreInst &SI) {
    if (!SI.getOperand(0)->hasName()) {
      // is rhs has no name, means the value is a imm val
      // then we shouldn't add to the table
      return "";
    }
    return SI.getOperand(0)->getName().str();
  }

  static const std::tuple<const std::string, const std::string> makeVariables(
      BitCastInst &BCI) {
    return std::make_tuple(
        makeVariable(BCI.getFunction(),
                     BCI.getOperand(0)->getName()),      // target
        makeVariable(static_cast<Instruction &>(BCI)));  // source
  }

 private:
  static const std::string makeVariable(Function *F, const StringRef &VarName) {
    return makeVariable(F, VarName.str());
  }

  static const std::string makeVariable(Function *F,
                                        const std::string &VarName) {
    if (!F->hasName()) {
      F->setName("tmp_func");
    }
    return F->getName().str() + "/" + VarName;
  }
};

struct PointerAnalysisVisitor
    : public InstVisitor<PointerAnalysisVisitor, void> {
  DDlogAnalyser Analyser;

  void visitLoadInst(LoadInst &LI) {
    if (!LI.getType()->isVoidTy()) {
      Analyser.addLoadInst(ObjectMaker::makeVariable(LI),
                           ObjectMaker::makeObject(LI.getType()));
    }
    // errs() << "visiting load instruction: \t" << LI << "\n";
  }

  void visitStoreInst(StoreInst &SI) {
    // errs() << "visiting store instruction: \t" << SI << "\n";
    auto VarSource = ObjectMaker::makeVariableSource(SI);
    if (VarSource == "") {
      return;
    }
    auto VarTarget = ObjectMaker::makeVariableTarget(SI);
    Analyser.addStoreInst(VarTarget, VarSource);
  }

  void visitBitCastInst(BitCastInst &BCI) {
    // FIXME: should we treat BitCast as StoreInst?
    if (!BCI.getType()->isVoidTy()) {
      auto [Target, Source] = ObjectMaker::makeVariables(BCI);
      Analyser.addStoreInst(Target, Source);
    }
    // errs() << "visiting bitcast instruction: \t" << BCI << "\n";
  }

  void visitAllocaInst(AllocaInst &AI) {
    // errs() << "visiting alloca instruction: \t" << AI << "\n";
    if (AI.isArrayAllocation()) {
      // TODO: array allocation
    } else {
      Analyser.addAllocInst(ObjectMaker::makeVariable(AI),
                            ObjectMaker::makeObject(AI.getAllocatedType()));
    }
  }

  void finish() { Analyser.commit(); }
};

struct PointerAnalysis : public PassInfoMixin<PointerAnalysis> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    // errs() << "Pointer analysis start..\n";

    PointerAnalysisVisitor Visitor;

    for (auto &F : M) {
      for (auto &BB : F) {
        for (auto &I : BB) {
          // errs() << "visiting instruction: \t\t" << I << "\n";
          Visitor.visit(I);
        }
      }
    }

    Visitor.finish();

    return PreservedAnalyses::all();
  }
};
}  // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "PointerAnalysis", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineEarlySimplificationEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel OL) {
                  MPM.addPass(PointerAnalysis());
                });
          }};
}
