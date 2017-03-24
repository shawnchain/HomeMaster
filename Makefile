build_iracc:
	@echo "============================================================"
	@echo "= Building IRACC Gate                                      ="
	@echo "============================================================"
	@make -C iracc

all: build_iracc

clean:
	@make -C iracc clean

distclean:
