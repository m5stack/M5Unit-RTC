# M5Unit-RTC

## Overview

### SKU:U126

M5Stack-**UNIT RTC** related programs.compatible with BM8563 and HYM8563.

## Related Link

[Document & Datasheet - M5Unit-RTC](https://docs.m5stack.com/en/unit/UNIT%20RTC)

## License

[M5Unit-RTC - MIT](LICENSE)

---

## M5UnitUnified

Library for Unit RTC using [M5UnitUnified](https://github.com/m5stack/M5UnitUnified).
M5UnitUnified is a library for unified handling of various M5 units products.

### Supported units
- Unit RTC (SKU:U126)

### Include file
```cpp
#include <M5UnitUnifiedRTC.h> // For UnitUnified
//#include <Unit_RTC.h> // When using M5UnitUnified, do not use it at the same time as conventional libraries
```

### Required Libraries:
- [M5UnitUnified](https://github.com/m5stack/M5UnitUnified)
- [M5Utility](https://github.com/m5stack/M5Utility)
- [M5HAL](https://github.com/m5stack/M5HAL)

### Examples
See also [examples/UnitUnified](examples/UnitUnified)

### Doxygen document
[GitHub Pages](https://m5stack.github.io/M5Unit-RTC/)

If you want to generate documents on your local machine, execute the following command

```
bash docs/doxy.sh
```

It will output it under docs/html
If you want to output Git commit hashes to html, do it for the git cloned folder.

#### Required
- [Doxygen](https://www.doxygen.nl/)
- [pcregrep](https://formulae.brew.sh/formula/pcre2)
- [Git](https://git-scm.com/) (Output commit hash to html)

 
