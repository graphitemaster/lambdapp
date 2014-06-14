CC ?= clang
LAMBDAPP := ../lambdapp

# Since we create an 'obj/' directory and BSD's make defaults to doing weird
# things depending on the stars and weather etc. make sure we don't end up
# in 'obj/'
.OBJDIR: .

all: $(LAMBDAPP) test.log

$(LAMBDAPP):
	$(MAKE) -C ..

test.log: $(LAMBDAPP) $(TESTS)
	./runtests.sh
