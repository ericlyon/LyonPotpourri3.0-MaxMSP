{
    "patcher": {
        "fileversion": 1,
        "appversion": {
            "major": 9,
            "minor": 1,
            "revision": 5,
            "architecture": "x64",
            "modernui": 1
        },
        "classnamespace": "box",
        "rect": [ 2345.0, 1274.0, 627.0, 548.0 ],
        "default_fontsize": 10.0,
        "boxes": [
            {
                "box": {
                    "format": 6,
                    "id": "obj-14",
                    "maxclass": "flonum",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "", "bang" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 233.0, 255.0, 50.0, 20.0 ]
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 10.0,
                    "hidden": 1,
                    "id": "obj-6",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 447.0, 311.0, 49.0, 20.0 ],
                    "text": "autohelp"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-11",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 345.0, 345.0, 60.0, 20.0 ],
                    "text": "feedback"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-9",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 198.0, 412.0, 136.0, 20.0 ],
                    "text": "line follows \"read\" head"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "format": 6,
                    "id": "obj-7",
                    "maxclass": "flonum",
                    "maximum": 0.95,
                    "minimum": -0.95,
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "", "bang" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 285.0, 345.0, 56.0, 22.0 ]
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "hidden": 1,
                    "id": "obj-5",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 70.0, 204.0, 73.0, 22.0 ],
                    "text": "loadmess 2"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "hidden": 1,
                    "id": "obj-3",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 6.0, 404.0, 87.0, 22.0 ],
                    "text": "loadmess 100"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-34",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 142.0, 414.0, 47.0, 22.0 ],
                    "text": "line $1"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-31",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "signal" ],
                    "patching_rect": [ 233.0, 352.0, 49.0, 22.0 ],
                    "text": "+~ 600"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-30",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "signal" ],
                    "patching_rect": [ 233.0, 322.0, 47.0, 22.0 ],
                    "text": "*~ 500"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-29",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "signal" ],
                    "patching_rect": [ 233.0, 291.0, 66.0, 22.0 ],
                    "text": "cycle~ 0.2"
                }
            },
            {
                "box": {
                    "id": "obj-28",
                    "interpinlet": 1,
                    "maxclass": "gain~",
                    "multichannelvariant": 0,
                    "numinlets": 2,
                    "numoutlets": 2,
                    "outlettype": [ "signal", "" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 86.0, 424.0, 24.0, 85.0 ]
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-27",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "signal" ],
                    "patching_rect": [ 101.0, 234.0, 47.0, 22.0 ],
                    "text": "noise~"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-26",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "signal" ],
                    "patching_rect": [ 161.0, 319.0, 37.0, 22.0 ],
                    "text": "*~"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-25",
                    "maxclass": "newobj",
                    "numinlets": 7,
                    "numoutlets": 1,
                    "outlettype": [ "signal" ],
                    "patching_rect": [ 179.0, 227.0, 103.0, 22.0 ],
                    "saved_object_attributes": {
                        "attack": 10.0,
                        "gain1": 0.7,
                        "gain2": 0.1,
                        "release": 100.0,
                        "sustain": 50.0
                    },
                    "text": "el.adsr~"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-24",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "signal" ],
                    "patching_rect": [ 179.0, 190.0, 97.0, 22.0 ],
                    "text": "el.samm~ 120 1"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-23",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "clear" ],
                    "patching_rect": [ 95.0, 168.0, 44.0, 22.0 ],
                    "text": "t clear"
                }
            },
            {
                "box": {
                    "allowdrag": 0,
                    "fontsize": 12.0,
                    "id": "obj-22",
                    "items": [ 1000, ",", 2000, ",", 3000 ],
                    "maxclass": "umenu",
                    "numinlets": 1,
                    "numoutlets": 3,
                    "outlettype": [ "int", "", "" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 21.0, 131.0, 100.0, 22.0 ]
                }
            },
            {
                "box": {
                    "allowdrag": 0,
                    "buffername": "vdb.help",
                    "fontsize": 12.0,
                    "id": "obj-19",
                    "maxclass": "waveform~",
                    "numinlets": 5,
                    "numoutlets": 6,
                    "outlettype": [ "float", "float", "float", "float", "list", "" ],
                    "patching_rect": [ 126.0, 451.0, 256.0, 64.0 ],
                    "setmode": 1
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "format": 6,
                    "id": "obj-15",
                    "maxclass": "flonum",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "", "bang" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 15.0, 248.0, 56.0, 22.0 ]
                }
            },
            {
                "box": {
                    "id": "obj-4",
                    "maxclass": "ezdac~",
                    "numinlets": 2,
                    "numoutlets": 0,
                    "patching_rect": [ 75.0, 524.0, 45.0, 45.0 ]
                }
            },
            {
                "box": {
                    "fontface": 0,
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-2",
                    "maxclass": "newobj",
                    "numinlets": 3,
                    "numoutlets": 2,
                    "outlettype": [ "signal", "float" ],
                    "patching_rect": [ 76.0, 388.0, 329.0, 22.0 ],
                    "text": "el.vdb~ @buffername vdb.help @chans 1 @maxdelay 3000"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-1",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "float", "bang" ],
                    "patching_rect": [ 179.0, 158.0, 128.0, 22.0 ],
                    "text": "buffer~ vdb.help 3000"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-13",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 18.0, 325.0, 77.0, 22.0 ],
                    "text": "maxdelay $1"
                }
            },
            {
                "box": {
                    "fontface": 3,
                    "fontname": "Arial",
                    "fontsize": 20.871338,
                    "id": "obj-8",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 10.0, 8.0, 485.0, 30.0 ],
                    "text": "el.vdb~",
                    "textcolor": [ 0.2, 0.2, 0.2, 1.0 ],
                    "varname": "autohelp_top_title"
                }
            },
            {
                "box": {
                    "fontface": 0,
                    "fontname": "Arial",
                    "fontsize": 12.754705,
                    "id": "obj-10",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 10.0, 36.0, 485.0, 21.0 ],
                    "text": "A buffer-based delay line",
                    "textcolor": [ 0.2, 0.2, 0.2, 1.0 ],
                    "varname": "autohelp_top_digest"
                }
            },
            {
                "box": {
                    "fontface": 0,
                    "fontname": "Arial",
                    "fontsize": 11.595187187194824,
                    "id": "obj-21",
                    "linecount": 2,
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 10.0, 57.0, 485.0, 32.0 ],
                    "text": "el.vdb~treats a buffer as a delayline with internal feedback (thus no lower limit on delay from vector size).",
                    "varname": "autohelp_top_description"
                }
            },
            {
                "box": {
                    "bgcolor": [ 0.0, 0.0, 0.0, 0.0 ],
                    "bgoncolor": [ 0.0, 0.0, 0.0, 0.0 ],
                    "fontface": 3,
                    "fontlink": 1,
                    "fontname": "Arial",
                    "fontsize": 12.754705,
                    "id": "obj-32",
                    "legacytextcolor": 1,
                    "maxclass": "textbutton",
                    "numinlets": 1,
                    "numoutlets": 3,
                    "outlettype": [ "", "", "int" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 352.7071533203125, 22.0, 147.2928466796875, 14.249397277832031 ],
                    "spacing_x": 0.0,
                    "spacing_y": 0.0,
                    "text": "open el.vdb~ reference",
                    "textcolor": [ 0.27, 0.35, 0.47, 1.0 ],
                    "textoncolor": [ 0.27, 0.35, 0.47, 1.0 ],
                    "textovercolor": [ 0.4, 0.5, 0.65, 1.0 ],
                    "underline": 1,
                    "usebgoncolor": 1,
                    "usetextovercolor": 1,
                    "varname": "autohelp_top_ref"
                }
            },
            {
                "box": {
                    "fontface": 1,
                    "fontname": "Arial",
                    "fontsize": 11.595187,
                    "id": "obj-35",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 452.0, 315.0, 100.0, 19.0 ],
                    "text": "See Also:",
                    "textcolor": [ 0.0, 0.0, 0.0, 1.0 ],
                    "varname": "autohelp_see_title"
                }
            },
            {
                "box": {
                    "allowdrag": 0,
                    "fontface": 0,
                    "fontname": "Arial",
                    "fontsize": 11.595187,
                    "id": "obj-36",
                    "items": [ "(Objects:)", ",", "el.chopper~", ",", "stutter~", ",", "<separator>" ],
                    "maxclass": "umenu",
                    "numinlets": 1,
                    "numoutlets": 3,
                    "outlettype": [ "int", "", "" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 452.0, 335.0, 130.0, 21.0 ],
                    "varname": "autohelp_see_menu"
                }
            },
            {
                "box": {
                    "angle": 0.0,
                    "background": 1,
                    "grad1": [ 0.88, 0.98, 0.78, 1.0 ],
                    "grad2": [ 0.9, 0.9, 0.9, 1.0 ],
                    "id": "obj-33",
                    "maxclass": "panel",
                    "mode": 1,
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 5.0, 5.0, 495.0, 52.0 ],
                    "proportion": 0.39,
                    "varname": "autohelp_top_panel"
                }
            },
            {
                "box": {
                    "angle": 0.0,
                    "background": 1,
                    "bgcolor": [ 0.85, 0.85, 0.85, 0.75 ],
                    "border": 2,
                    "bordercolor": [ 0.5, 0.5, 0.5, 0.75 ],
                    "id": "obj-37",
                    "maxclass": "panel",
                    "mode": 0,
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 447.0, 311.0, 140.0, 50.0 ],
                    "proportion": 0.39,
                    "varname": "autohelp_see_panel"
                }
            },
            {
                "box": {
                    "attr": "gain2",
                    "id": "obj-39",
                    "maxclass": "attrui",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 351.353577, 190.0, 150.0, 20.0 ]
                }
            }
        ],
        "lines": [
            {
                "patchline": {
                    "destination": [ "obj-2", 0 ],
                    "midpoints": [ 27.5, 375.0, 85.5, 375.0 ],
                    "source": [ "obj-13", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-29", 0 ],
                    "source": [ "obj-14", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-13", 0 ],
                    "source": [ "obj-15", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-28", 0 ],
                    "midpoints": [ 85.5, 417.0, 95.5, 417.0 ],
                    "source": [ "obj-2", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-34", 0 ],
                    "midpoints": [ 395.5, 411.5, 151.5, 411.5 ],
                    "source": [ "obj-2", 1 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-15", 0 ],
                    "midpoints": [ 71.0, 235.0, 24.5, 235.0 ],
                    "order": 1,
                    "source": [ "obj-22", 1 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-23", 0 ],
                    "midpoints": [ 71.0, 166.0, 104.5, 166.0 ],
                    "order": 0,
                    "source": [ "obj-22", 1 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-1", 0 ],
                    "midpoints": [ 104.5, 200.0, 156.0, 200.0, 156.0, 150.0, 188.5, 150.0 ],
                    "source": [ "obj-23", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-25", 0 ],
                    "source": [ "obj-24", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-26", 1 ],
                    "source": [ "obj-25", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-2", 0 ],
                    "midpoints": [ 170.5, 367.0, 85.5, 367.0 ],
                    "source": [ "obj-26", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-26", 0 ],
                    "midpoints": [ 110.5, 287.0, 170.5, 287.0 ],
                    "source": [ "obj-27", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-4", 1 ],
                    "midpoints": [ 95.5, 519.0, 110.5, 519.0 ],
                    "order": 0,
                    "source": [ "obj-28", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-4", 0 ],
                    "midpoints": [ 95.5, 519.0, 84.5, 519.0 ],
                    "order": 1,
                    "source": [ "obj-28", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-30", 0 ],
                    "source": [ "obj-29", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-28", 0 ],
                    "hidden": 1,
                    "midpoints": [ 15.5, 432.0, 81.0, 432.0, 81.0, 429.0, 95.5, 429.0 ],
                    "source": [ "obj-3", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-31", 0 ],
                    "source": [ "obj-30", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-2", 1 ],
                    "midpoints": [ 242.5, 379.0, 240.5, 379.0 ],
                    "source": [ "obj-31", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-19", 0 ],
                    "midpoints": [ 151.5, 439.0, 136.0, 439.0, 136.0, 448.0, 135.5, 448.0 ],
                    "source": [ "obj-34", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-25", 0 ],
                    "midpoints": [ 360.853577, 218.0, 188.5, 218.0 ],
                    "source": [ "obj-39", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-22", 0 ],
                    "hidden": 1,
                    "source": [ "obj-5", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-2", 2 ],
                    "midpoints": [ 294.5, 377.0, 395.5, 377.0 ],
                    "source": [ "obj-7", 0 ]
                }
            }
        ],
        "autosave": 0
    }
}