"""micro 2.3.4 -> bitbox MK2 2.3.4 address map, used by tools/port_mk2.py to port portkit's sources.

Each entry: micro address -> (MK2 address, evidence). Evidence kinds:
  code vN/M     tools/micro2mk2.py instruction-window match, N votes for the offset (next best M), exact instruction
  data unique   the micro bytes (flash pointers masked) occur once in the MK2 image
  ram literals  the micro literal's loading instruction matched, its MK2 literal read (votes / total)
  manual        checked by hand, reason given
  layout        this project's own RAM layout on the MK2 (free gap measured in the Renode canary run, see
                tests/test_ram.py), not a firmware address
Status of every entry is "vermutet" until a test or the device confirms the behaviour that depends on it.
An address list, no firmware content.
"""

MAP = {
    # ---- image layout
    0x08040000: (0x08040000, "manual: load base, same vector table layout"),
    0x080E3E90: (0x080E4C50, "manual: end of the stock image = cave origin (674896 bytes)"),
    # ---- automatic (tools/micro2mk2.py, filtered: exact instruction, >= 40 votes, >= 2x runner-up; or unique data)
    0x08040e90: (0x08040e94, 'code v71/2'),  # load.c:2
    0x080423cc: (0x080422c8, 'code v88/2'),  # load.c:2
    0x08042458: (0x08042354, 'code v90/5'),  # load.c:3,sound.py:23
    0x08042464: (0x08042360, 'code v87/5'),  # load.c:4,sound.py:25
    0x08042474: (0x08042370, 'code v81/5'),  # load.c:4,sound.py:24
    0x08042480: (0x0804237c, 'code v76/7'),  # load.c:4,sound.py:26
    0x08060366: (0x0805ff6e, 'code v96/23'),  # load.c:20,sound.py:27
    0x08069e4c: (0x0806a24c, 'code v79/30'),  # filter.c:50
    0x08069e7c: (0x0806a27c, 'code v82/20'),  # filter.c:44,clipshift.c:305,reverb.c:39
    0x08069ed4: (0x0806a2d4, 'code v90/12'),  # filter.c:110,reverb.c:40
    0x0806ab7e: (0x0806af74, 'code v83/7'),  # sound.py:29
    0x0806b34a: (0x0806b73e, 'code v96/3'),  # filter.c:106,fidelity.py:13
    0x0806b89a: (0x0806bc8e, 'code v96/6'),  # reverb.c:12,reverb.py:11
    0x08070748: (0x080714e4, 'code v76/2'),  # sound.py:27
    0x08070754: (0x080714f0, 'code v76/2'),  # load.c:6,sound.py:27
    0x08076d54: (0x08078e90, 'data unique'),  # clipshift.c:329
    0x08076d8c: (0x08078ec8, 'code v43/9'),  # clipshift.c:322,sound.py:31,pages.py:4
    0x08076f48: (0x0807902e, 'code v66/0'),  # clipshift.c:323
    0x0807d37e: (0x0807f7e2, 'code v50/23'),  # clipshift.c:46,sound.py:11
    0x0808056c: (0x08082e90, 'code v76/14'),  # filter.c:135
    0x080807b6: (0x080830da, 'code v96/22'),  # clipshift.c:302
    0x080836c8: (0x080884c8, 'code v96/8'),  # clipshift.c:323
    0x08083744: (0x08088544, 'code v96/8'),  # reverb.c:15
    0x08087d82: (0x08087e26, 'code v65/0'),  # sound.py:33
    0x08087de8: (0x08087e8c, 'code v74/0'),  # sound.py:34
    0x0808949e: (0x08089356, 'code v90/0'),  # filter.c:134,sound.py:65
    0x080894d0: (0x08089388, 'code v94/0'),  # filter.c:323,clipshift.c:331,fastget.c:2
    0x0808951e: (0x080893d6, 'code v96/4'),  # filter.c:133,reverb.c:41,sound.py:14
    0x08089548: (0x08089400, 'code v96/6'),  # filter.c:133,sound.py:16
    0x0808954e: (0x08089406, 'code v96/9'),  # sound.py:68
    0x080896f8: (0x080895b0, 'code v96/42'),  # filter.c:133,sound.py:18
    0x08089922: (0x080897da, 'code v96/42'),  # filter.c:133,sound.py:19
    0x08089aa0: (0x08089958, 'code v96/6'),  # reverb.c:14
    0x08089aa8: (0x08089960, 'code v96/6'),  # reverb.c:92,reverb.py:12
    0x08089abe: (0x08089976, 'code v96/0'),  # reverb.c:99,reverb.py:13
    0x08089f98: (0x08089e50, 'code v96/0'),  # sound.py:68
    0x08089fc4: (0x08089e7c, 'code v96/0'),  # pages.py:6
    0x080a01bc: (0x080a1564, 'data unique'),  # filter.c:38
    0x080a0240: (0x080a15e8, 'data unique'),  # filter.c:39
    0x080a1400: (0x080a27a8, 'code v96/10'),  # filter.c:457,fidelity.py:21
    0x080a1494: (0x080a283c, 'code v96/0'),  # filter.c:457,fidelity.py:24
    0x080a19f4: (0x080a2d9c, 'code v96/24'),  # filter.c:40
    0x080a31e0: (0x080a4588, 'code v96/7'),  # filter.c:42
    0x080a31ec: (0x080a4594, 'code v96/7'),  # filter.c:41
    0x080a321c: (0x080a45c4, 'code v96/7'),  # stretch.c:32
    0x080a3228: (0x080a45d0, 'code v96/7'),  # filter.c:43
    0x080a32d4: (0x080a467c, 'code v96/7'),  # stretch.c:34
    0x080a44c8: (0x080a5870, 'code v48/15'),  # clipshift.c:7,clipshift_thunk.S:4,sound.py:41
    0x080a4500: (0x080a58a8, 'code v71/15'),  # clipshift.c:7,clipshift_thunk.S:3,sound.py:39
    0x080a45ac: (0x080a5954, 'code v96/0'),  # clipshift.c:40,stretch.c:30
    0x080a4624: (0x080a59cc, 'code v69/22'),  # clipshift.c:8,clipshift_thunk.S:5,sound.py:43
    0x080a6c3c: (0x080a7e1c, 'data unique'),  # reverb.c:13
    0x080a6ca4: (0x080a7e84, 'code v43/3'),  # filter.c:51
    0x080a9690: (0x080aa870, 'code v75/14'),  # reverb.c:4
    0x080a9912: (0x080aaaf2, 'code v96/0'),  # reverb.c:6,reverb_thunk.S:2,reverb.py:14
    0x080a9afc: (0x080aacdc, 'data unique'),  # reverb.c:4
    0x080a9ec4: (0x080ab0a4, 'code v96/0'),  # reverb.c:6,reverb_thunk.S:2,reverb.py:14
    0x080ad960: (0x080ae9bc, 'code v96/11'),  # filter_thunk.S:4,fidelity.py:15
    0x080adc4c: (0x080aeca8, 'code v96/43'),  # filter.c:455,fidelity.py:18
    0x080adccc: (0x080aed28, 'code v96/8'),  # filter.c:492
    0x080add4c: (0x080aeda8, 'code v96/12'),  # filter.c:566,fidelity.py:24
    0x080addd8: (0x080aee34, 'code v96/24'),  # filter.c:567,fidelity.py:25
    0x080addfc: (0x080aee58, 'code v96/24'),  # filter.c:567,fidelity.py:26
    0x080ade84: (0x080aeee0, 'code v96/15'),  # filter.c:492
    0x080adf00: (0x080aef5c, 'code v96/24'),  # filter.c:566,fidelity.py:21
    0x080adf24: (0x080aef80, 'code v96/24'),  # filter.c:566,fidelity.py:22
    0x080adf48: (0x080aefa4, 'code v96/24'),  # filter.c:566,fidelity.py:23
    0x080aee92: (0x080afeee, 'code v96/7'),  # filter.c:544,fidelity.py:18
    0x080af0d8: (0x080b0134, 'code v96/3'),  # filter.c:544,fidelity.py:19
    0x080af19a: (0x080b01f6, 'code v96/1'),  # filter.c:544,fidelity.py:20
    0x080b17aa: (0x080b2802, 'code v96/6'),  # filter.c:320
    0x080b1aea: (0x080b2b42, 'code v41/0'),  # filter.c:51
    0x080b40fc: (0x080b5224, 'code v96/2'),  # clipshift.c:27
    0x080b4644: (0x080b576c, 'code v96/3'),  # clipshift.c:159,sound.py:38
    0x080b47d2: (0x080b58fa, 'code v96/5'),  # clipshift.c:8,clipshift_thunk.S:5,sound.py:43
    0x080b490c: (0x080b5a34, 'code v96/2'),  # clipshift.c:23,clipshift_thunk.S:8,sound.py:38
    0x080b49b4: (0x080b5adc, 'code v96/3'),  # clipshift.c:26,clipshift_thunk.S:10,sound.py:60
    0x080b4a68: (0x080b5b90, 'code v96/2'),  # clipshift.c:23,clipshift_thunk.S:9,sound.py:59
    0x080b4b4c: (0x080b5c74, 'code v96/5'),  # clipshift.c:8,clipshift_thunk.S:5,sound.py:44
    0x080b55ac: (0x080b66d4, 'code v96/0'),  # clipshift.c:7,stretch.c:3,sound.py:21
    0x080b5630: (0x080b6758, 'code v96/0'),  # clipshift_thunk.S:3,sound.py:39
    0x080b567e: (0x080b67a6, 'code v96/0'),  # clipshift_thunk.S:4,sound.py:41
    0x080b56f2: (0x080b681a, 'code v96/0'),  # clipshift_thunk.S:4,sound.py:42
    0x080b5720: (0x080b6848, 'code v96/0'),  # clipshift_thunk.S:3,sound.py:40
    0x080b5728: (0x080b6850, 'code v96/0'),  # stretch.c:3
    0x080b58fc: (0x080b6a24, 'code v96/1'),  # stretch.c:3,sound.py:21
    0x080b5aa8: (0x080b6bd0, 'code v90/1'),  # clipshift.c:6,sound.py:54
    0x080b5ad4: (0x080b6bfc, 'code v90/1'),  # filter_thunk.S:8
    0x080b5ccc: (0x080b6df4, 'code v96/0'),  # filter_thunk.S:6,fidelity.py:16
    0x080b5f2e: (0x080b7056, 'code v96/2'),  # clipshift.c:15,clipshift_thunk.S:6,sound.py:56
    0x080b611a: (0x080b7242, 'code v96/3'),  # clipshift.c:15,clipshift_thunk.S:6,sound.py:57
    0x080b6fd4: (0x080b80fc, 'code v96/6'),  # clipshift.c:322
    0x080b7386: (0x080b84ae, 'code v96/5'),  # clipshift.c:301
    0x080b74e4: (0x080b860c, 'data unique'),  # clipshift.c:301,sound.py:71
    0x080b827c: (0x080b93a4, 'code v96/1'),  # filter.c:196
    0x080b8420: (0x080b9548, 'code v96/1'),  # filter.c:8,fidelity.py:15
    0x080b8794: (0x080b98bc, 'code v61/30'),  # filter.c:159
    0x080c21cc: (0x080c3348, 'code v96/0'),  # clipshift.c:309
    0x080c2414: (0x080c3590, 'code v96/0'),  # filter.c:101
    0x080c241c: (0x080c3598, 'code v96/0'),  # filter.c:101
    0x080c33ac: (0x080c4528, 'code v96/0'),  # clipshift.c:308
    0x080c33b4: (0x080c4530, 'code v96/0'),  # clipshift.c:308
    0x080c33b8: (0x080c4534, 'code v96/0'),  # clipshift.c:308
    0x080c33bc: (0x080c4538, 'code v96/0'),  # clipshift.c:309
    0x080c33c0: (0x080c453c, 'code v96/0'),  # clipshift.c:309
    0x080c3488: (0x080c45d0, 'code v48/11'),  # clipshift.c:308
    0x080c4f7c: (0x080c60cc, 'data unique'),  # clipshift.c:300,sound.py:70
    0x080c5760: (0x080c68ec, 'data unique'),  # cave.py:20
    0x080c5770: (0x080c68fc, 'data unique'),  # cave.py:21
    0x24013cb0: (0x24015170, 'ram literals 63/63'),  # filter.c:328,reverb.c:45
    # ---- manual
    0x08077F5E: (0x08079FBC, "manual: app pad-record init loop, same strides (0x30 / -0x3c0), side by side"),
    0x0807D358: (0x0807F7BC, "manual: parameter command dispatcher, prologue and the 0x1c5 compare side by side"),
    0x0807D854: (0x0807F8DA, "manual: dispatcher FX-record store set (app + 0x2200 + row * 0x18), same sequence"),
    0x08089F8C: (0x08089E44, "manual: pad record reset, v96, function start (window alignment)"),
    0x08093D44: (0x080952D8, "manual: BL target at the MK2 audio-task call sites 0x08042354 / 0x08042370"),
    0x08093E48: (0x08095364, "manual: BL target at the MK2 audio-task call sites 0x08042360 / 0x0804237c"),
    0x080A4650: (0x080A59F8, "manual: stream stereo flag getter, ldrb r0,[r0,#0x1c]; bx lr, same neighbours"),
    0x080BCD58: (0x080BDE58, "manual: snprintf, same prologue"),
    0x080C4770: (0x080C58C0, "manual: Sync list, literal of the MK2 Sync registration 0x0806af6c, same 7 names"),
    0x080C5780: (0x080C690C, "manual: after the version slot (comment)"),
    # pad page tables: MK2 has 5 pages x 36 ids (micro 6 x 45); patches/sound.py and pages.py are MK2-specific
    0x080C6A1C: (0x080C7BD4, "manual: Clip table A (builder literal order)"),
    0x080C6C38: (0x080C7D3C, "manual: Clip table B"),
    0x080C6EEC: (0x080C7F3C, "manual: Granular table A"),
    0x080C7108: (0x080C80A4, "manual: Granular table B"),
    0x080C76C0: (0x080C84F4, "manual: Sample table A"),
    0x080C78DC: (0x080C865C, "manual: Sample table B"),
    0x080C7AF8: (0x080C87C4, "manual: Multisample table (no Pos page)"),
    0x080C7E28: (0x080C8A04, "manual: Slicer table A"),
    0x080C8044: (0x080C8B6C, "manual: Slicer table B"),
    # ---- RAM: this project's layout in the MK2 gap 0x2406ad98 (initial SP) .. 0x24070dc0 (first firmware use)
    0x240692A8: (0x2406AD98, "manual: initial SP from the vector table"),
    0x2406F4E0: (0x24070DC0, "layout: first firmware use above the stack (Renode canary, MK2)"),
    0x2406A000: (0x2406B000, "layout: fastget cache 0x800"),
    0x2406A800: (0x2406B800, "layout: master distortion state 0x300"),
    0x2406AB00: (0x2406BB00, "layout: recmon struct 0x20"),
    0x2406C000: (0x2406BC00, "layout: micro patcher store (comment only, not used on the MK2)"),
    0x2406C07C: (0x2406BC7C, "layout: (comment only)"),
    0x2406C100: (0x2406BD00, "layout: PARK"),
    0x2406C10B: (0x2406BD0B, "layout: (pad pages, not used)"),
    0x2406C110: (0x2406BD10, "layout: (pad pages, not used)"),
    0x2406C114: (0x2406BD14, "layout: (pad pages, not used)"),
    0x2406C115: (0x2406BD15, "layout: (pad pages, not used)"),
    0x2406C180: (0x2406BD80, "layout: clip diagnostics"),
    0x2406C1C0: (0x2406BDC0, "layout: load meter 0x40"),
    0x2406C400: (0x2406C000, "layout: stretch grain states 0xa00"),
    0x2406CDFF: (0x2406C9FF, "layout: last byte of the stretch block"),
    0x2406CE00: (0x2406CA00, "layout: stretch block end"),
    0x2406CFF0: (0x2406CBF0, "layout: reverb object pointer"),
    0x2406D000: (0x2406CC00, "layout: reverb RS1 (FX2)"),
    0x2406D100: (0x2406CD00, "layout: reverb RS2 (FX3)"),
    0x2406D200: (0x2406CE00, "layout: FX3 state"),
    0x2406D400: (0x2406D000, "layout: master page state"),
    0x2406D410: (0x2406D010, "layout: (old distortion state, comment)"),
    0x2406D580: (0x2406D180, "layout: master SSL state"),
    0x2406E000: (0x2406E000, "layout: dry copy / monitor signal 0x1000 (unchanged, inside the MK2 gap)"),
    0x2406E800: (0x2406E800, "layout: second half of the dry copy"),
    # ---- tests (tests/*.py)
    0x08042DC0: (0x08043B4C, "code v42/20: bump allocator (also the param-init alloc, tools/emu_params_mk2.py)"),
    0x0806B33A: (0x0806B72E, "code v88/3"),
    0x0806B34E: (0x0806B742, "code v96/4"),
    0x0807074C: (0x080714E8, "code v76/2"),
    0x08070758: (0x080714F4, "code v75/2"),
    0x080894FA: (0x080893B2, "code v96/0: store set"),
    0x080A028E: (0x080A1636, "code v33/32 (comment: vcvt.f64 in the Res curve)"),
    0x080B44FC: (0x080B5624, "code v96/0"),
    0x080B6CE8: (0x080B7E10, "code v96/8: clip parameter handler"),
    0x080B81F0: (0x080B9318, "code v60/29"),
    0x080BC000: (0x080BD100, "manual: scan limit, end of code (test only)"),
    0x080BF4CC: (0x080C05EC, "code v30/26, test only (message object word): vermutet"),
    0x080C0EC0: (0x080C203C, "data unique"),
    0x080C0EC8: (0x080C2044, "data unique"),
    0x24000000: (0x24000000, "manual: memory map base (test emulator)"),
    0x24040000: (0x24040000, "manual: test allocator area"),
    0x2407F000: (0x2407F000, "manual: test stack"),
    0x30000000: (0x30000000, "manual: D2 SRAM scratch (test emulator)"),
    0x31000000: (0x31000000, "manual: end of the test scratch map"),
    0x08042DFC: (0x08043B88, "code v42/36: DTCM allocator; the three allocators all shift by +0xd8c (0x08042dc0 / 0x08042e44)"),
    0x080A9C94: (0x080AAE74, "code v60/27: plate state init; the reverb block shifts by +0x11e0 (0x080a9912 -> 0x080aaaf2)"),
}
