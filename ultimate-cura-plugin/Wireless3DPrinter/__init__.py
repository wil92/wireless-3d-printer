def getMetaData():
    return {}


def register(app):
    from .output_device import Wireless3DPrinterPlugin
    return {"output_device": Wireless3DPrinterPlugin()}
