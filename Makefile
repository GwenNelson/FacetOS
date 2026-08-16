BUILD := build

.PHONY: all configure clean run

all: $(BUILD)/build.ninja
	ninja -C $(BUILD)

$(BUILD)/build.ninja:
	cmake \
		-G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/external/seL4/gcc.cmake \
		-S . \
		-B $(BUILD)

configure:
	rm -rf $(BUILD)
	$(MAKE) $(BUILD)/build.ninja

run: all
	$(BUILD)/simulate

clean:
	rm -rf $(BUILD)
