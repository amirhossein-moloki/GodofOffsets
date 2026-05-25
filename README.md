# Universal Offset Dumper

ابزاری Native، فوق‌سریع و حرفه‌ای برای استخراج آفست‌ها و اسکن حافظه در ویندوز (x86/x64).

## ✨ ویژگی‌های برجسته
- **Zydis Integration:** محاسبه دقیق آدرس‌های RIP-Relative برای دستورات LEA, MOV, CALL, JMP.
- **Universal PE Parser:** پشتیبانی کامل از پردازش‌های ۳۲ و ۶۴ بیتی با استخراج بخش‌های PE.
- **High-Performance Scanner:** استفاده از SSE/SIMD برای اسکن AOB و Multi-threading برای سرعت حداکثری.
- **Pointer Scanner:** الگوریتم جستجوی زنجیره اشاره‌گر با قابلیت تشخیص چرخه (Cycle Detection).
- **Structure Dumper:** قابلیت تعریف و دامپ ساختارهای پیچیده حافظه به صورت JSON/CSV.
- **Responsive UI:** رابط کاربری ImGui Docking با پردازش‌های پس‌زمینه (Asynchronous).

## 🛡 معماری Stealth Mode
این ابزار دارای یک لایه‌ی انتزاعی برای مدیریت حافظه است که اجازه می‌دهد بین حالت‌های مختلف جابجا شوید:
1. **Standard Mode:** استفاده از `OpenProcess` و `ReadProcessMemory` استاندارد.
2. **Stealth Mode (Placeholder):** در حال حاضر یک اسکلت‌بندی ماژولار است که از `PROCESS_QUERY_LIMITED_INFORMATION` استفاده می‌کند. این بخش طوری طراحی شده که توسعه‌دهندگان بتوانند به راحتی درایورهای Kernel-mode یا تکنیک‌های Handle Hijacking (مانند VDM) را بدون تغییر در منطق اسکنر، به پروژه تزریق کنند.

## 🚀 راهنمای راه‌اندازی
### پیش‌نیازها
- یک کامپایلر C++20 (MSVC 2022 یا Clang)
- CMake 3.20+

### کامپایل
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## 🛠 نحوه استفاده
1. برنامه را اجرا کنید.
2. از تب **Process**، نام پردازش هدف را وارد کرده و متصل شوید.
3. در تب **Memory Scanner** می‌توانید انواع داده‌ها را جستجو کنید.
4. در تب **Signature Scanner** آفست‌های از پیش تعریف شده در `signatures.json` را استخراج کنید.
5. از تب **Structure Dumper** برای مشاهده و ذخیره ساختارهای حافظه استفاده کنید.

## ⚠️ هشدار
این ابزار برای اهداف تحقیقاتی و آموزشی است. استفاده در بازی‌های آنلاین دارای آنتی‌چیت فعال (مانند BattlEye) در حالت Standard Mode منجر به شناسایی و بن خواهد شد.

---
*توسعه داده شده برای جامعه مهندسی معکوس.*
