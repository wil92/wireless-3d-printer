def getMetaData():
    return {"stage": {"name": "Local Printer", "weight": 5}}


def register(app):
    from .output_device import Wireless3DPrinterPlugin
    from .local_printer import LocalPrinterStage
    return {"output_device": Wireless3DPrinterPlugin(), "stage": LocalPrinterStage()}
