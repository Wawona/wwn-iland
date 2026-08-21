/Users/8amps/GhidraVibe/dyld-extracted/modeb-25F80/SkyLight:
(__TEXT,__text) section
_SLSInitialize:
18707c1f0:	48 b3 32 d0	adrp	x8, 415338 ; 0x1ec6e6000
18707c1f4:	08 6d 42 f9	ldr	x8, [x8, #0x4d8]
18707c1f8:	1f 05 00 b1	cmn	x8, #0x1
18707c1fc:	41 00 00 54	b.ne	0x18707c204
18707c200:	c0 03 5f d6	ret
18707c204:	40 b3 32 d0	adrp	x0, 415338 ; 0x1ec6e6000
18707c208:	00 60 13 91	add	x0, x0, #0x4d8
18707c20c:	01 9c 33 b0	adrp	x1, 422785 ; 0x1ee3fd000
18707c210:	21 80 34 91	add	x1, x1, #0xd20
18707c214:	77 98 04 14	b	0x1871a23f0 ; symbol stub for: _dispatch_once
