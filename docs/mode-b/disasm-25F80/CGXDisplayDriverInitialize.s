/Users/8amps/GhidraVibe/dyld-extracted/modeb-25F80/CoreDisplay:
(__TEXT,__text) section
_CGXDisplayDriverInitialize:
182ffcd40:	7f 23 03 d5	pacibsp
182ffcd44:	ff 83 03 d1	sub	sp, sp, #0xe0
182ffcd48:	fc 6f 08 a9	stp	x28, x27, [sp, #0x80]
182ffcd4c:	fa 67 09 a9	stp	x26, x25, [sp, #0x90]
182ffcd50:	f8 5f 0a a9	stp	x24, x23, [sp, #0xa0]
182ffcd54:	f6 57 0b a9	stp	x22, x21, [sp, #0xb0]
182ffcd58:	f4 4f 0c a9	stp	x20, x19, [sp, #0xc0]
182ffcd5c:	fd 7b 0d a9	stp	x29, x30, [sp, #0xd0]
182ffcd60:	fd 43 03 91	add	x29, sp, #0xd0
182ffcd64:	68 48 31 d0	adrp	x8, 403726 ; 0x1e590a000
182ffcd68:	08 f1 45 f9	ldr	x8, [x8, #0xbe0]
182ffcd6c:	08 01 40 f9	ldr	x8, [x8]
182ffcd70:	a8 83 1a f8	stur	x8, [x29, #-0x58]
182ffcd74:	08 64 33 f0	adrp	x8, 420995 ; 0x1e9c7f000
182ffcd78:	08 19 41 f9	ldr	x8, [x8, #0x230]
182ffcd7c:	1f 09 3f d6	blraaz	x8
182ffcd80:	f3 03 00 aa	mov	x19, x0
182ffcd84:	08 64 33 d0	adrp	x8, 420994 ; 0x1e9c7e000
182ffcd88:	08 81 47 f9	ldr	x8, [x8, #0xf00]
182ffcd8c:	1f 05 00 b1	cmn	x8, #0x1
182ffcd90:	61 5c 00 54	b.ne	0x182ffd91c
182ffcd94:	08 64 33 d0	adrp	x8, 420994 ; 0x1e9c7e000
182ffcd98:	09 e1 7b 39	ldrb	w9, [x8, #0xef8]
182ffcd9c:	69 e2 01 39	strb	w9, [x19, #0x78]
182ffcda0:	08 64 33 d0	adrp	x8, 420994 ; 0x1e9c7e000
182ffcda4:	08 e1 37 91	add	x8, x8, #0xdf8
182ffcda8:	3f 05 00 71	cmp	w9, #0x1
182ffcdac:	61 00 00 54	b.ne	0x182ffcdb8
182ffcdb0:	1f 7d a9 88	cas	w9, wzr, [x8]
182ffcdb4:	04 00 00 14	b	0x182ffcdc4
182ffcdb8:	09 00 80 52	mov	w9, #0x0
182ffcdbc:	2a 00 80 52	mov	w10, #0x1
182ffcdc0:	0a 7d a9 88	cas	w9, w10, [x8]
182ffcdc4:	14 64 33 f0	adrp	x20, 420995 ; 0x1e9c7f000
182ffcdc8:	88 1e 44 f9	ldr	x8, [x20, #0x838]
182ffcdcc:	1f 09 3f d6	blraaz	x8
182ffcdd0:	ff 03 01 39	strb	wzr, [sp, #0x40]
182ffcdd4:	c1 7c 35 90	adrp	x1, 438168 ; 0x1edf94000
182ffcdd8:	21 a0 0c 91	add	x1, x1, #0x328
182ffcddc:	e2 03 01 91	add	x2, sp, #0x40
182ffcde0:	24 94 ff 97	bl	_CDCFDictionaryGetBoolean
182ffcde4:	e8 03 41 39	ldrb	w8, [sp, #0x40]
182ffcde8:	1f 1d 00 72	tst	w8, #0xff
182ffcdec:	e8 03 80 1a	csel	w8, wzr, w0, eq
182ffcdf0:	09 64 33 d0	adrp	x9, 420994 ; 0x1e9c7e000
182ffcdf4:	28 29 36 39	strb	w8, [x9, #0xd8a]
182ffcdf8:	88 1e 44 f9	ldr	x8, [x20, #0x838]
182ffcdfc:	1f 09 3f d6	blraaz	x8
182ffce00:	ff 03 01 39	strb	wzr, [sp, #0x40]
182ffce04:	c1 7c 35 90	adrp	x1, 438168 ; 0x1edf94000
182ffce08:	21 20 0d 91	add	x1, x1, #0x348
182ffce0c:	e2 03 01 91	add	x2, sp, #0x40
182ffce10:	18 94 ff 97	bl	_CDCFDictionaryGetBoolean
182ffce14:	e8 03 41 39	ldrb	w8, [sp, #0x40]
182ffce18:	1f 1d 00 72	tst	w8, #0xff
182ffce1c:	e8 03 80 1a	csel	w8, wzr, w0, eq
182ffce20:	09 64 33 d0	adrp	x9, 420994 ; 0x1e9c7e000
182ffce24:	28 2d 36 39	strb	w8, [x9, #0xd8b]
182ffce28:	88 1e 44 f9	ldr	x8, [x20, #0x838]
182ffce2c:	1f 09 3f d6	blraaz	x8
182ffce30:	ff 03 01 39	strb	wzr, [sp, #0x40]
182ffce34:	c1 7c 35 90	adrp	x1, 438168 ; 0x1edf94000
182ffce38:	21 a0 0d 91	add	x1, x1, #0x368
182ffce3c:	e2 03 01 91	add	x2, sp, #0x40
182ffce40:	0c 94 ff 97	bl	_CDCFDictionaryGetBoolean
182ffce44:	e8 03 41 39	ldrb	w8, [sp, #0x40]
182ffce48:	1f 1d 00 72	tst	w8, #0xff
182ffce4c:	e8 03 80 1a	csel	w8, wzr, w0, eq
182ffce50:	09 64 33 d0	adrp	x9, 420994 ; 0x1e9c7e000
182ffce54:	28 21 36 39	strb	w8, [x9, #0xd88]
182ffce58:	88 1e 44 f9	ldr	x8, [x20, #0x838]
182ffce5c:	1f 09 3f d6	blraaz	x8
182ffce60:	ff 03 01 39	strb	wzr, [sp, #0x40]
182ffce64:	c1 7c 35 90	adrp	x1, 438168 ; 0x1edf94000
182ffce68:	21 20 0e 91	add	x1, x1, #0x388
182ffce6c:	e2 03 01 91	add	x2, sp, #0x40
182ffce70:	00 94 ff 97	bl	_CDCFDictionaryGetBoolean
182ffce74:	e8 03 41 39	ldrb	w8, [sp, #0x40]
182ffce78:	1f 1d 00 72	tst	w8, #0xff
182ffce7c:	e8 03 80 1a	csel	w8, wzr, w0, eq
182ffce80:	09 64 33 d0	adrp	x9, 420994 ; 0x1e9c7e000
182ffce84:	28 31 36 39	strb	w8, [x9, #0xd8c]
182ffce88:	88 1e 44 f9	ldr	x8, [x20, #0x838]
182ffce8c:	1f 09 3f d6	blraaz	x8
182ffce90:	c1 7c 35 90	adrp	x1, 438168 ; 0x1edf94000
182ffce94:	21 a0 0e 91	add	x1, x1, #0x3a8
182ffce98:	e2 03 01 91	add	x2, sp, #0x40
182ffce9c:	f5 93 ff 97	bl	_CDCFDictionaryGetBoolean
182ffcea0:	35 00 80 52	mov	w21, #0x1
182ffcea4:	e8 63 33 b0	adrp	x8, 420989 ; 0x1e9c79000
182ffcea8:	15 a5 31 39	strb	w21, [x8, #0xc69]
182ffceac:	68 02 00 b0	adrp	x8, 77 ; 0x183049000
182ffceb0:	08 49 0a 91	add	x8, x8, #0x292 ; literal pool for: "on"
182ffceb4:	69 02 00 b0	adrp	x9, 77 ; 0x183049000
182ffceb8:	29 b5 2a 91	add	x9, x9, #0xaad ; literal pool for: "init_page_flip"
182ffcebc:	60 02 00 b0	adrp	x0, 77 ; 0x183049000
182ffcec0:	00 5c 2a 91	add	x0, x0, #0xa97 ; literal pool for: "void init_page_flip()"
182ffcec4:	e9 23 00 a9	stp	x9, x8, [sp]
182ffcec8:	62 02 00 b0	adrp	x2, 77 ; 0x183049000
182ffcecc:	42 8c 09 91	add	x2, x2, #0x263 ; literal pool for: "%s: page flip mode is %s"
182ffced0:	01 87 82 52	mov	w1, #0x1438
182ffced4:	72 1c fd 97	bl	__ZN11CoreDisplay6Logger4InfoEPKciS2_z
182ffced8:	60 02 00 b0	adrp	x0, 77 ; 0x183049000
182ffcedc:	00 80 2c 91	add	x0, x0, #0xb20 ; literal pool for: "/AppleInternal/OrderFiles/CoreGraphics.order"
182ffcee0:	01 00 80 52	mov	w1, #0x0
182ffcee4:	c1 73 00 94	bl	0x183019de8 ; symbol stub for: _access
182ffcee8:	60 03 00 34	cbz	w0, 0x182ffcf54
182ffceec:	88 1e 44 f9	ldr	x8, [x20, #0x838]
182ffcef0:	1f 09 3f d6	blraaz	x8
182ffcef4:	c1 7c 35 90	adrp	x1, 438168 ; 0x1edf94000
182ffcef8:	21 a0 1b 91	add	x1, x1, #0x6e8
182ffcefc:	3b 6c 00 94	bl	0x183017fe8 ; symbol stub for: _CFDictionaryGetValue
182ffcf00:	60 02 00 b4	cbz	x0, 0x182ffcf4c
182ffcf04:	f4 03 00 aa	mov	x20, x0
182ffcf08:	34 6c 00 94	bl	0x183017fd8 ; symbol stub for: _CFDictionaryGetTypeID
182ffcf0c:	f5 03 00 aa	mov	x21, x0
182ffcf10:	e0 03 14 aa	mov	x0, x20
