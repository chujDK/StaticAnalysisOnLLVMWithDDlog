SHELL = /bin/sh

DDLOG_SOURCE_NAME_PREFIX := pointer_analysis
DDLOG_GENERATED_DIR = $(DDLOG_SOURCE_NAME_PREFIX)_ddlog
DDLOG_LIB = $(DDLOG_GENERATED_DIR)/target/release/lib$(DDLOG_SOURCE_NAME_PREFIX)_ddlog.a

.PHONY:all
all: analysis
	@echo "done"

PointerAnalysis.so: pointer-analysis.cc $(DDLOG_LIB)
	clang++ `llvm-config-14 --cxxflags` -Wl,--whole-archive \
	$(DDLOG_LIB) \
	-fno-rtti -fPIC -shared pointer-analysis.cc -o PointerAnalysis.so \
	-DDEBUG `llvm-config-14 --ldflags` -DLLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING=1 \
	-lpthread -ldl -lm -Wl,--no-whole-archive -fuse-ld=mold

analysis: PointerAnalysis.so
	clang++ `llvm-config-14 --cxxflags` -fno-discard-value-names -fpass-plugin=./PointerAnalysis.so ./target/a.cc -o a.out

$(DDLOG_GENERATED_DIR): $(DDLOG_SOURCE_NAME_PREFIX).dl
	ddlog -i $(DDLOG_SOURCE_NAME_PREFIX).dl

$(DDLOG_LIB): $(DDLOG_GENERATED_DIR)
	@echo $(DDLOG_GENERATED_DIR)
	@echo $(DDLOG_LIB)
	cd $(DDLOG_GENERATED_DIR) && cargo build --release

.PHONY:clean
clean:
	rm -f PointerAnalysis.so a.out
	rm -rf $(DDLOG_SOURCE_NAME_PREFIX)_ddlog
