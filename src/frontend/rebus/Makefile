CC     = gcc
CFLAGS = -Wall -Wno-unused-function -Wno-unused-variable \
         -Wno-incompatible-pointer-types -g -O0 -I.
BISON  = bison
FLEX   = flex

SRCS   = rebus.tab.c lex.rebus.c rebus_print.c rebus_emit.c rebus_main.c
OBJS   = $(SRCS:.c=.o)

.PHONY: all clean test roundtrip

all: rebus

rebus: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lfl

rebus.tab.c rebus.tab.h: rebus.y
	$(BISON) -d -o rebus.tab.c rebus.y

lex.rebus.c: rebus.l rebus.tab.h
	$(FLEX) -o lex.rebus.c rebus.l

%.o: %.c rebus.tab.h rebus.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f rebus *.o rebus.tab.c rebus.tab.h lex.rebus.c

# ---- smoke tests ----
# Run the two examples from TR 84-9 through the parser and check for errors.
test: rebus
	@echo "=== Smoke test: word-count example ==="
	@echo 'function main()\
	  letter := &lcase || &ucase\
	  wpat := break(letter) & span(letter) . word\
	  count := table()\
	  while text := input do\
	    while text ?- wpat do\
	      count[word] +:= 1\
	  if result := sort(count) then {\
	    output := "Word count:"\
	    output := ""\
	    i := 0\
	    repeat output := rpad(result[i +:= 1, 1], 15) || lpad(result[i, 2], 4)\
	  }\
	  else output := "There are no words"\
	end' | ./rebus -p && echo "PASS: word-count" || echo "FAIL: word-count"
	@echo ""
	@echo "=== Smoke test: record declaration ==="
	@printf 'record complex(r, i)\nfunction main()\n  z := complex(1.0, 2.3)\n  return z\nend\n' \
	  | ./rebus -p && echo "PASS: record" || echo "FAIL: record"
	@echo ""
	@echo "=== Smoke test: test/rebus/word_count.reb (if present) ==="
	@if [ -f ../../test/rebus/word_count.reb ]; then \
	  ./rebus -p ../../test/rebus/word_count.reb && echo "PASS" || echo "FAIL"; \
	fi

# ---- round-trip tests (M-REBUS) ----
roundtrip: rebus
	@bash ../../test/rebus/run_roundtrip.sh
