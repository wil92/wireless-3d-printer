"""Formatting for printer file sizes."""


def format_file_size(size):
    units = ("B", "KB", "MB", "GB", "TB", "PB", "EB")
    divisor = 1
    unit = 0
    while unit < len(units) - 1 and size >= divisor * 1024:
        divisor *= 1024
        unit += 1

    if unit == 0:
        return "{} B".format(size)

    tenths = (size * 10 + divisor // 2) // divisor
    if tenths >= 10240 and unit < len(units) - 1:
        divisor *= 1024
        unit += 1
        tenths = (size * 10 + divisor // 2) // divisor
    value = str(tenths // 10)
    if tenths % 10:
        value += ".{}".format(tenths % 10)
    return "{} {}".format(value, units[unit])
