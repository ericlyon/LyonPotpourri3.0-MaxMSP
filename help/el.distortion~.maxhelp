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
        "rect": [ 1648.0, 1301.0, 589.0, 489.0 ],
        "boxes": [
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "hidden": 1,
                    "id": "obj-13",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 256.0, 265.0, 57.0, 22.0 ],
                    "text": "autohelp"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-32",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 28.0, 121.0, 113.0, 20.0 ],
                    "text": "drag soundfile here",
                    "textcolor": [ 1.0, 1.0, 1.0, 1.0 ]
                }
            },
            {
                "box": {
                    "bordercolor": [ 0.133333, 0.717647, 0.454902, 1.0 ],
                    "id": "obj-30",
                    "maxclass": "dropfile",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "", "" ],
                    "patching_rect": [ 24.0, 114.0, 120.0, 36.0 ]
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-29",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 139.0, 175.0, 50.0, 22.0 ],
                    "text": "loop $1"
                }
            },
            {
                "box": {
                    "id": "obj-27",
                    "maxclass": "toggle",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "int" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 139.0, 153.0, 20.0, 20.0 ]
                }
            },
            {
                "box": {
                    "id": "obj-26",
                    "maxclass": "toggle",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "int" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 89.0, 153.0, 20.0, 20.0 ]
                }
            },
            {
                "box": {
                    "id": "obj-24",
                    "interpinlet": 1,
                    "maxclass": "gain~",
                    "multichannelvariant": 0,
                    "numinlets": 2,
                    "numoutlets": 2,
                    "outlettype": [ "signal", "" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 90.0, 296.0, 20.0, 72.0 ]
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "hidden": 1,
                    "id": "obj-5",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "bang" ],
                    "patching_rect": [ 198.0, 114.0, 54.0, 21.0 ],
                    "text": "loadbang"
                }
            },
            {
                "box": {
                    "bgcolor": [ 0.403922, 0.403922, 0.403922, 1.0 ],
                    "coldcolor": [ 0.0, 0.658824, 0.0, 1.0 ],
                    "id": "obj-6",
                    "interval": 100,
                    "maxclass": "meter~",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "float" ],
                    "patching_rect": [ 144.0, 421.0, 80.0, 13.0 ],
                    "tepidcolor": [ 0.6, 0.729412, 0.0, 1.0 ],
                    "warmcolor": [ 0.85098, 0.85098, 0.0, 1.0 ]
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "id": "obj-7",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 42.0, 183.0, 50.0, 21.0 ],
                    "text": "open $1"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "hidden": 1,
                    "id": "obj-8",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 198.0, 139.0, 48.0, 21.0 ],
                    "text": "0.1 0.3"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "hidden": 1,
                    "id": "obj-9",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "float", "float" ],
                    "patching_rect": [ 198.0, 161.0, 226.0, 21.0 ],
                    "text": "unpack 0. 0."
                }
            },
            {
                "box": {
                    "id": "obj-11",
                    "maxclass": "ezdac~",
                    "numinlets": 2,
                    "numoutlets": 0,
                    "patching_rect": [ 89.0, 389.0, 45.0, 45.0 ]
                }
            },
            {
                "box": {
                    "bgcolor": [ 0.866667, 0.866667, 0.866667, 1.0 ],
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "format": 6,
                    "htricolor": [ 0.87, 0.82, 0.24, 1.0 ],
                    "id": "obj-14",
                    "maxclass": "flonum",
                    "maximum": 1.0,
                    "minimum": 0.05,
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "", "bang" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 325.0, 218.0, 46.0, 21.0 ],
                    "textcolor": [ 0.0, 0.0, 0.0, 1.0 ],
                    "tricolor": [ 0.75, 0.75, 0.75, 1.0 ],
                    "triscale": 0.9
                }
            },
            {
                "box": {
                    "bgcolor": [ 0.866667, 0.866667, 0.866667, 1.0 ],
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "format": 6,
                    "htricolor": [ 0.87, 0.82, 0.24, 1.0 ],
                    "id": "obj-15",
                    "maxclass": "flonum",
                    "maximum": 0.9,
                    "minimum": 0.0,
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "", "bang" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 197.0, 218.0, 49.0, 21.0 ],
                    "textcolor": [ 0.0, 0.0, 0.0, 1.0 ],
                    "tricolor": [ 0.75, 0.75, 0.75, 1.0 ],
                    "triscale": 0.9
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "id": "obj-18",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 2,
                    "outlettype": [ "signal", "bang" ],
                    "patching_rect": [ 89.0, 209.0, 50.0, 21.0 ],
                    "text": "sfplay~"
                }
            },
            {
                "box": {
                    "color": [ 0.87451, 0.321569, 0.321569, 1.0 ],
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "id": "obj-19",
                    "maxclass": "newobj",
                    "numinlets": 3,
                    "numoutlets": 1,
                    "outlettype": [ "signal" ],
                    "patching_rect": [ 89.0, 265.0, 110.0, 21.0 ],
                    "text": "el.distortion~ 0.1 0.3"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "id": "obj-20",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 195.0, 194.0, 136.0, 19.0 ],
                    "text": "\"knee\" of transfer function"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 11.0,
                    "id": "obj-21",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 333.0, 194.0, 155.0, 19.0 ],
                    "text": "cutoff of transfer function"
                }
            },
            {
                "box": {
                    "fontface": 3,
                    "fontname": "Arial",
                    "fontsize": 20.871338,
                    "id": "obj-16",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 10.0, 8.0, 485.0, 30.0 ],
                    "text": "el.distortion~",
                    "textcolor": [ 0.2, 0.2, 0.2, 1.0 ],
                    "varname": "autohelp_top_title"
                }
            },
            {
                "box": {
                    "fontface": 0,
                    "fontname": "Arial",
                    "fontsize": 12.754705,
                    "id": "obj-17",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 10.0, 36.0, 485.0, 21.0 ],
                    "text": "A simple distortion processor.",
                    "textcolor": [ 0.2, 0.2, 0.2, 1.0 ],
                    "varname": "autohelp_top_digest"
                }
            },
            {
                "box": {
                    "fontface": 0,
                    "fontname": "Arial",
                    "fontsize": 11.595187187194824,
                    "id": "obj-23",
                    "linecount": 2,
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 10.0, 57.0, 489.0, 32.0 ],
                    "text": "el.distortion~ implements a simple transfer function form of clipping. It is similar to clip~ but different enough to be worthwhile.",
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
                    "id": "obj-25",
                    "legacytextcolor": 1,
                    "maxclass": "textbutton",
                    "numinlets": 1,
                    "numoutlets": 3,
                    "outlettype": [ "", "", "int" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 316.57920837402344, 22.0, 183.42079162597656, 14.249397277832031 ],
                    "spacing_x": 0.0,
                    "spacing_y": 0.0,
                    "text": "open el.distortion~ reference",
                    "textcolor": [ 0.3870000000000001, 0.4850000000000001, 0.6320000000000001, 1.0 ],
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
                    "id": "obj-31",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 261.0, 269.0, 100.0, 19.0 ],
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
                    "id": "obj-33",
                    "items": [ "(Objects:)", ",", "clip~", ",", "kink~", ",", "<separator>" ],
                    "maxclass": "umenu",
                    "numinlets": 1,
                    "numoutlets": 3,
                    "outlettype": [ "int", "", "" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 261.0, 289.0, 130.0, 21.0 ],
                    "varname": "autohelp_see_menu"
                }
            },
            {
                "box": {
                    "angle": 0.0,
                    "background": 1,
                    "grad1": [ 0.88, 0.98, 0.78, 1.0 ],
                    "grad2": [ 0.9, 0.9, 0.9, 1.0 ],
                    "id": "obj-28",
                    "maxclass": "panel",
                    "mode": 1,
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 5.0, 5.0, 495.0, 52.0 ],
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
                    "id": "obj-34",
                    "maxclass": "panel",
                    "mode": 0,
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 256.0, 265.0, 140.0, 50.0 ],
                    "varname": "autohelp_see_panel"
                }
            }
        ],
        "lines": [
            {
                "patchline": {
                    "destination": [ "obj-19", 2 ],
                    "midpoints": [ 334.5, 250.0, 189.5, 250.0 ],
                    "source": [ "obj-14", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-19", 1 ],
                    "midpoints": [ 206.5, 241.0, 144.0, 241.0 ],
                    "source": [ "obj-15", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-19", 0 ],
                    "source": [ "obj-18", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-24", 0 ],
                    "source": [ "obj-19", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-11", 1 ],
                    "midpoints": [ 98.0, 378.0, 124.5, 378.0 ],
                    "order": 1,
                    "source": [ "obj-24", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-11", 0 ],
                    "order": 2,
                    "source": [ "obj-24", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-6", 0 ],
                    "midpoints": [ 98.0, 378.0, 153.5, 378.0 ],
                    "order": 0,
                    "source": [ "obj-24", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-18", 0 ],
                    "source": [ "obj-26", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-29", 0 ],
                    "source": [ "obj-27", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-18", 0 ],
                    "midpoints": [ 148.5, 197.0, 98.5, 197.0 ],
                    "source": [ "obj-29", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-7", 0 ],
                    "midpoints": [ 33.5, 175.0, 51.5, 175.0 ],
                    "source": [ "obj-30", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-8", 0 ],
                    "hidden": 1,
                    "source": [ "obj-5", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-18", 0 ],
                    "midpoints": [ 51.5, 204.0, 98.5, 204.0 ],
                    "source": [ "obj-7", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-9", 0 ],
                    "hidden": 1,
                    "source": [ "obj-8", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-14", 0 ],
                    "hidden": 1,
                    "midpoints": [ 414.5, 197.0, 334.5, 197.0 ],
                    "source": [ "obj-9", 1 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-15", 0 ],
                    "hidden": 1,
                    "midpoints": [ 207.5, 201.0, 206.5, 201.0 ],
                    "source": [ "obj-9", 0 ]
                }
            }
        ],
        "autosave": 0
    }
}