.PHONY: all build clean mrproper

PROJECT_NAME	:=  sys-con
SOURCE_DIR		:=	source
OUT_DIR			:=	out
COMMON_DIR		:=	common
GIT_BRANCH		:= $(shell git symbolic-ref --short HEAD | sed s/[^a-zA-Z0-9_-]/_/g)
GIT_HASH 		:= $(shell git rev-parse --short HEAD)
GIT_TAG 		:= $(shell git describe --tags `git rev-list --tags --max-count=1`)
BUILD_VERSION	:= $(GIT_TAG:v%=%)-$(GIT_BRANCH)-$(GIT_HASH)

all: build
	rm -rf $(OUT_DIR)/atmosphere $(OUT_DIR)/config $(OUT_DIR)/switch
	mkdir -p $(OUT_DIR)/atmosphere/contents/690000000000000D/flags
	mkdir -p $(OUT_DIR)/config/sys-con
	mkdir -p $(OUT_DIR)/switch/
	touch $(OUT_DIR)/atmosphere/contents/690000000000000D/flags/boot2.flag
	cp $(SOURCE_DIR)/Sysmodule/out/nintendo_nx_arm64_armv8a/release/Sysmodule.nsp $(OUT_DIR)/atmosphere/contents/690000000000000D/exefs.nsp
	cp $(SOURCE_DIR)/AppletCompanion/sys-con.nro $(OUT_DIR)/switch/sys-con.nro
	cp -r $(COMMON_DIR)/. $(OUT_DIR)/
	@echo [DONE] sys-con compiled successfully. All files have been placed in $(OUT_DIR)/

build:
	$(MAKE) -C $(SOURCE_DIR)

dist: all
	cd $(OUT_DIR); zip -r $(PROJECT_NAME)-$(BUILD_VERSION).zip ./*; cd ../;


clean:
	$(MAKE) -C $(SOURCE_DIR) clean
	rm -rf $(OUT_DIR)/atmosphere $(OUT_DIR)/config $(OUT_DIR)/switch
	
mrproper:
	$(MAKE) -C $(SOURCE_DIR) mrproper
	rm -rf $(OUT_DIR)
	
