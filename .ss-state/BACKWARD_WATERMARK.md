# SS-BLOCK-BACKWARD Watermark — GROUND TRUTH

**Last verified block:** BUKPTR/LSTPTR (lines 11112-11113)  
**True watermark:** 11112  
**Next block to verify:** ARG1CL (line 11111) going backward  
**Git commit of last BWD work:** 3ebf18fa  

## WHY THE MILESTONE DOC WAS WRONG

MILESTONE-SS-BLOCK-BACKWARD.md showed watermark=12120 (SUCCPT).
That was the state after the VERY FIRST backward sessions.
Then 66 more BWD commits happened — MANY sessions of work —
but NOBODY UPDATED THE MILESTONE DOC WATERMARK.

The milestone doc is append-only for the completed table but
THE WATERMARK LINE MUST BE UPDATED AFTER EVERY BLOCK.

## HOW TO FIND TRUE WATERMARK (never trust the doc alone)

```bash
cd /home/claude/one4all
git log --oneline | grep "BWD" | head -1
# → get the block name, then:
grep -n "^BLOCKNAME\b" /home/claude/work/snobol4-2.3.3/v311.sil
# → that line number IS the watermark
```

## HOW TO FIND NEXT BLOCK

```bash
TRUE_WATERMARK=11112
grep -n "^[A-Z][A-Z0-9]*\b" /home/claude/work/snobol4-2.3.3/v311.sil \
  | awk -F: '$1<'$TRUE_WATERMARK | tail -1
```

## UPDATED: 2026-04-09 SSB-4 session correction
