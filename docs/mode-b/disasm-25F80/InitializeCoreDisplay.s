/Users/8amps/GhidraVibe/dyld-extracted/modeb-25F80/CoreDisplay:
(__TEXT,__text) section
_InitializeCoreDisplay:
182ff3ca0:	7f 23 03 d5	pacibsp
182ff3ca4:	f4 4f be a9	stp	x20, x19, [sp, #-0x20]!
182ff3ca8:	fd 7b 01 a9	stp	x29, x30, [sp, #0x10]
182ff3cac:	fd 43 00 91	add	x29, sp, #0x10
182ff3cb0:	e1 03 00 aa	mov	x1, x0
182ff3cb4:	60 64 33 90	adrp	x0, 421004 ; 0x1e9c7f000
182ff3cb8:	00 80 08 91	add	x0, x0, #0x220
182ff3cbc:	02 d7 80 52	mov	w2, #0x6b8
182ff3cc0:	f6 99 00 94	bl	0x18301a498 ; symbol stub for: _memcpy
182ff3cc4:	c5 98 00 94	bl	0x183019fd8 ; symbol stub for: _dispatch_pthread_root_queue_copy_current
182ff3cc8:	e8 03 00 aa	mov	x8, x0
182ff3ccc:	c9 b5 34 f0	adrp	x9, 431803 ; 0x1ec6ae000
182ff3cd0:	20 cd 46 f9	ldr	x0, [x9, #0xd98]
182ff3cd4:	28 cd 06 f9	str	x8, [x9, #0xd98]
182ff3cd8:	d3 b5 34 f0	adrp	x19, 431803 ; 0x1ec6ae000
182ff3cdc:	60 00 00 b4	cbz	x0, 0x182ff3ce8
182ff3ce0:	68 d2 46 f9	ldr	x8, [x19, #0xda0]
182ff3ce4:	1f 09 3f d6	blraaz	x8
182ff3ce8:	10 00 00 90	adrp	x16, 0 ; 0x182ff3000
182ff3cec:	10 32 32 91	add	x16, x16, #0xc8c
182ff3cf0:	f0 23 c1 da	paciza	x16
182ff3cf4:	70 d2 06 f9	str	x16, [x19, #0xda0]
182ff3cf8:	fd 7b 41 a9	ldp	x29, x30, [sp, #0x10]
182ff3cfc:	f4 4f c2 a8	ldp	x20, x19, [sp], #0x20
182ff3d00:	ff 0f 5f d6	retab
182ff3d04:	f8 8f 00 94	bl	___clang_call_terminate
