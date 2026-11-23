def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "FPSCounter",
        "AutoRotate2D",
        "AutoRotate3D",
        "Bobbing3D",
        "ScreenShakeBase",
        "ScreenShake2D",
        "ScreenShake3D",
    ]


def get_doc_path():
    return "doc_classes"


def get_icons_path():
    return "icons"
