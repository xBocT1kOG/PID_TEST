"""Recreate the Ukrainian report and plots from the unmodified final recording.

Run from any directory: python analysis/plot_pid_data.py
Only CSV values are plotted; the 0..120 s panels are fixed opening excerpts.
"""
from pathlib import Path
import hashlib
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.pagesizes import A4
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Image, Table, TableStyle, PageBreak

ROOT = Path(__file__).resolve().parents[1]
CSV = ROOT / "data/pid_test_2026-09-28_12-02-10.csv"
PLOTS = ROOT / "analysis/plots"
REPORT = ROOT / "docs/pid_experiment_report_uk.pdf"
EXPECTED_SHA256 = "a8b722457a65949f4f3c5ffca99ba7362f67f8291a32b36995ffa53a16843479"
COLUMNS = "time_ms,target_rpm,raw_rpm,filtered_rpm,rpm_rate,filtered_rate,error,p,i,d,power".split(",")


def load_data():
    digest = hashlib.sha256(CSV.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError("Final dataset differs from the original recording")
    df = pd.read_csv(CSV)
    assert list(df.columns) == COLUMNS
    assert np.isfinite(df.to_numpy()).all()
    assert (df.time_ms.diff().dropna() == 500).all()
    assert set(df.target_rpm) == {1800, 2500}
    assert (df.raw_rpm % 60 == 0).all()
    assert df.power.between(0, 80).all()
    assert np.allclose(df.error, df.target_rpm - df.filtered_rpm, atol=0.11)
    df["t"] = (df.time_ms - df.time_ms.iloc[0]) / 1000
    segment = df.target_rpm.ne(df.target_rpm.shift()).cumsum()
    df["segment"] = segment
    df["age"] = (df.time_ms - df.groupby(segment).time_ms.transform("min")) / 1000
    return df, digest


def summarize(df):
    rows = []
    for scope, selected in [("all", df), ("age_ge_10s", df[df.age >= 10])]:
        for target, group in selected.groupby("target_rpm"):
            for signal in ["raw_rpm", "filtered_rpm"]:
                error = group[signal] - target
                rows.append(dict(scope=scope, target_rpm=int(target), signal=signal,
                                 n=len(group), mean=float(group[signal].mean()),
                                 mae=float(error.abs().mean()), sd=float(group[signal].std(ddof=1)),
                                 minimum=float(group[signal].min()), maximum=float(group[signal].max()),
                                 within_100_percent=float(100 * error.abs().le(100).mean())))
    return pd.DataFrame(rows)


def plot(df, name, title, fields, ylabel, window=False, power=False):
    view = df[df.t <= 120] if window else df
    fig, ax = plt.subplots(figsize=(7.25, 2.65), layout="constrained")
    for column, label, color, width in fields:
        ax.plot(view.t, view[column], label=label, color=color, lw=width,
                drawstyle="steps-post" if column == "target_rpm" else "default")
    if power:
        ax.axhline(80, color="#a34632", ls="--", lw=1, label="Ліміт 80%")
        ax.set_ylim(0, 85)
    ax.set(title=title, xlabel="Час від першого запису, с", ylabel=ylabel)
    ax.set_xlim(view.t.iloc[0], view.t.iloc[-1])
    ax.grid(alpha=.18)
    ax.spines[["top", "right"]].set_visible(False)
    ax.legend(loc="upper center", bbox_to_anchor=(.5, -.28), ncol=len(fields)+(1 if power else 0), frameon=False, fontsize=8)
    fig.savefig(PLOTS / f"{name}.png", dpi=200)
    plt.close(fig)


def build_report(df, stats, digest):
    font_dir = Path(matplotlib.get_data_path()) / "fonts/ttf"
    pdfmetrics.registerFont(TTFont("DV", str(font_dir / "DejaVuSans.ttf")))
    pdfmetrics.registerFont(TTFont("DV-Bold", str(font_dir / "DejaVuSans-Bold.ttf")))
    pdfmetrics.registerFontFamily("DV", normal="DV", bold="DV-Bold", italic="DV", boldItalic="DV-Bold")
    styles = {
        "title": ParagraphStyle("title", fontName="DV-Bold", fontSize=24, leading=29, textColor=colors.HexColor("#163d50"), spaceAfter=16),
        "h": ParagraphStyle("h", fontName="DV-Bold", fontSize=14, leading=18, textColor=colors.HexColor("#163d50"), spaceBefore=12, spaceAfter=8),
        "body": ParagraphStyle("body", fontName="DV", fontSize=10, leading=15, spaceAfter=9),
        "small": ParagraphStyle("small", fontName="DV", fontSize=8.3, leading=12, spaceAfter=8),
    }
    story = []
    def p(text, style="body"):
        story.append(Paragraph(text, styles[style]))
    def chart(name):
        story.append(Image(str(PLOTS / f"{name}.png"), width=503, height=184))
        story.append(Spacer(1, 7))
    def page(title):
        story.append(PageBreak())
        p(title, "title")
    def table(rows, widths):
        cells = [[Paragraph(str(cell), styles["small"]) for cell in row] for row in rows]
        t = Table(cells, colWidths=widths, hAlign="LEFT")
        t.setStyle(TableStyle([("BACKGROUND", (0,0), (-1,0), colors.HexColor("#e4edf1")),
                              ("VALIGN", (0,0), (-1,-1), "TOP"),
                              ("LINEBELOW", (0,0), (-1,0), .6, colors.HexColor("#7594a3")),
                              ("ROWBACKGROUNDS", (0,1), (-1,-1), [colors.white, colors.HexColor("#f5f7f8")]),
                              ("TOPPADDING", (0,0), (-1,-1), 6), ("BOTTOMPADDING", (0,0), (-1,-1), 3)]))
        story.append(t)
        story.append(Spacer(1, 10))

    p("PID-керування швидкістю вентилятора", "title")
    p("Навчальний експеримент · ESP32-S3 · 28 вересня 2026 року", "small")
    p("Мета роботи", "h")
    p("Метою роботи було практичне дослідження поведінки PID-регулятора на реальному об'єкті: впливу дискретного вимірювання RPM, фільтрації та окремих P/I/D складових. Лабораторна точність не була метою експерименту.")
    p("Експериментальний стенд", "h")
    p("ESP32-S3 керує 4-wire вентилятором Arctic Bionix F120 через інвертувальний транзисторний каскад з відкритим колектором. PWM: GPIO40, 20 kHz, 10 bit. Тахометр: GPIO39, 2 імпульси на оберт, зовнішній pull-up 10 kΩ. Вентилятор фізично закріплено для зменшення вібрації; одна лопать відсутня.")
    table([["Параметр", "Значення"], ["Цілі та період", "1800 / 2500 RPM; 60 циклів ≈ 30 с на ціль"], ["Вимірювання / керування", "500 ms; номінальний dt = 0.5 s"], ["PID", "KP = 0.04; KI = 0.003; KD = 0.003"], ["EMA", "RPM_FILTER_ALPHA = 0.25; RATE_FILTER_ALPHA = 0.35"], ["Команда", "Базова складова 40%; обмеження 0..80%"]], [177,326])
    p("Метод керування", "h")
    p("Кількість імпульсів за 0.5 с множиться на 60: один імпульс відповідає 60 RPM. EMA формує filtered_rpm, а похідна його зміни ділиться на dt і проходить другу EMA. P = KP × error, I = KI × integral, D = -KD × filtered_rate. Інтегратор приймає приріст лише тоді, коли він не поглиблює вихід за межі команди. Сума 40 + P + I + D обмежується до 0..80%.")
    p("Налаштування PID", "h")
    p("За описом автора, коефіцієнти добирали експериментально за raw/filtered RPM, поведінкою похідної, P/I/D та вихідної команди під час повторних переходів між цілями. Це добір за спостережуваною реакцією системи. Фінальний CSV показує результат; історії попередніх налаштувань у ньому немає.")

    page("Відстеження цільової RPM")
    p(f"Запис містить {len(df)} рядки, {df.t.iloc[-1]:.1f} с між першим і останнім вимірюванням та {df.segment.nunique()-1} зміни цілі. Усі інтервали дорівнюють 500 ms. Повні графіки використовують кожний рядок CSV без додаткового згладжування.")
    chart("rpm_full")
    p("Контролер багаторазово повертає швидкість в околицю обох цілей. Raw RPM зберігає помітні коливання; EMA зменшує їх, але додає затримку реакції.", "small")
    chart("rpm_opening")
    p("Детальний фрагмент - перші 120 с, обрані фіксованим правилом, а не за якістю регулювання. Видно початковий перехід та обидва напрями зміни цілі. Першу EMA ініціалізовано фактичним першим вимірюванням.", "small")
    p("Межі інтерпретації", "h")
    p("Крок тахометричного вимірювання становить 60 RPM. Точність також обмежують шум, 500 ms вікно, затримка фільтра та механічне пошкодження вентилятора. Відмінність raw і filtered RPM не є доказом точності відносно незалежного еталона.")

    page("Похідна та її фільтрація")
    p("Поле rpm_rate - це нефільтрована швидкість зміни <b>filtered_rpm</b>, а не похідна raw_rpm. Поле filtered_rate - результат другої EMA з α = 0.35. Саме воно використовується у D.")
    chart("rate_full")
    p(f"За всім записом SD rpm_rate = {df.rpm_rate.std():.1f} RPM/s, SD filtered_rate = {df.filtered_rate.std():.1f} RPM/s. Це опис розкиду сигналів разом із переходами, а не окрема оцінка вимірювального шуму.", "small")
    chart("rate_opening")
    p("На початковому фрагменті видно пригнічення швидких змін похідної та затримку після EMA. Додатковий фільтр робить D менш чутливим до коливань вимірювання.", "small")
    p("Чому derivative-on-measurement", "h")
    p("Похідна розраховується зі швидкості вентилятора, тому стрибок цілі не входить у D безпосередньо. D протидіє зростанню RPM і підтримує команду під час падіння RPM. Перше значення похідної дорівнює нулю; другу EMA ініціалізовано першою доступною похідною.")

    page("Складові PID і команда PWM")
    chart("pid_full")
    p(f"P реагує на поточну похибку, I накопичує повільнішу корекцію, D реагує на зміну виміряної швидкості. Діапазон D: {df.d.min():.2f}..{df.d.max():.2f} відсоткового пункта. Постійні 40% не входять у показані P/I/D.", "small")
    chart("power_full")
    p(f"CSV power - обмежена десяткова команда, діапазон {df.power.min():.2f}..{df.power.max():.2f}%. Перед PWM код відкидає дробову частину: q = (uint8_t)power; duty = 1023 - floor(1023 × q / 100). Отже, це команда актуатора, а не виміряна електрична потужність. Точний PWM не можна відновити з округленого CSV біля цілих значень.", "small")
    p("Anti-windup та обмеження", "h")
    p("Код блокує приріст інтеграла, якщо пробна команда перевищує 80% при додатній похибці або падає нижче 0% при від'ємній. У цьому записі немає насичення виходу чи нульових raw RPM. Тому відключення двигуна та роботу anti-windup у такому режимі цей CSV не демонструє.")

    page("Кількісні результати та висновки")
    p("Метрики охоплюють весь запис. Окремо наведено умовно усталені ділянки: від 10 с після початку кожного сегмента незмінної цілі, включно з початковим сегментом. Це однакове правило для всіх сегментів, а не підтвердження повного усталення. Відкинуто 860 рядків лише для цієї другої оцінки.")
    rows = [["Вибірка / ціль", "n", "Mean\nRPM", "MAE\nRPM", "SD\nRPM", "±100\nRPM, %"]]
    for scope, label in [("all", "Усі"), ("age_ge_10s", "Від 10 с")]:
        for target in [1800,2500]:
            r = stats[(stats.scope == scope) & (stats.target_rpm == target) & (stats.signal == "filtered_rpm")].iloc[0]
            rows.append([f"{label} / {target}", str(int(r.n)), f"{r['mean']:.0f}", f"{r.mae:.0f}", f"{r.sd:.0f}", f"{r.within_100_percent:.1f}"])
    table(rows, [148,40,76,76,76,87])
    p("Таблиця: filtered RPM. MAE = mean(|RPM - target|); SD - вибіркове стандартне відхилення (n - 1). Смуга ±100 RPM є описовим критерієм, а не вимогою точності. За рівних інтервалів частка рядків наближено відповідає частці часу.", "small")
    late = stats[(stats.scope == "age_ge_10s") & (stats.signal == "raw_rpm")]
    p("Порівняння з raw RPM", "h")
    p("На тих самих ділянках від 10 с: " + "; ".join(f"ціль {int(r.target_rpm)} RPM - mean {r['mean']:.0f}, MAE {r.mae:.0f}, SD {r.sd:.0f} RPM" for _, r in late.iterrows()) + ". Менший розкид filtered RPM показує ефект згладжування, але не усуває похибку вимірювання.")
    p("Висновки", "h")
    p("Контролер повторно досягає околиці 1800 і 2500 RPM та утримує швидкість навколо заданих значень. Після 10 с середня filtered RPM залишається приблизно на 65 RPM вище нижньої цілі та на 67 RPM нижче верхньої; ідеального регулювання немає.")
    p("EMA помітно покращує придатність RPM і похідної для керування, зберігаючи компроміс між згладжуванням та затримкою. Дискретність тахометра, шум, період вимірювання та відсутня лопать обмежують точність. Експеримент досяг навчальної мети: практичного розуміння PID на реальному об'єкті.")
    p("Джерела та відтворення", "h")
    p("Дані: data/pid_test_2026-09-28_12-02-10.csv.<br/>Алгоритм і параметри: src/main.c. Опис стенда й процесу налаштування: умови експерименту, надані автором.<br/>Графіки, метрики та PDF: python analysis/plot_pid_data.py.<br/>Повні метрики raw/filtered RPM: analysis/summary_metrics.csv.", "small")
    p("SHA-256 CSV: " + digest[:32] + "<br/>" + digest[32:], "small")

    def footer(canvas, doc):
        canvas.setStrokeColor(colors.HexColor("#d6e1e6"))
        canvas.line(46, 42, 549, 42)
        canvas.setFont("DV", 8)
        canvas.setFillColor(colors.HexColor("#526d79"))
        canvas.drawString(46, 28, "PID_TEST · Навчальний експеримент")
        canvas.drawRightString(549, 28, str(doc.page))
    doc = SimpleDocTemplate(str(REPORT), pagesize=A4, rightMargin=46, leftMargin=46,
                            topMargin=42, bottomMargin=56, title="PID-керування швидкістю вентилятора", author="PID_TEST")
    doc.build(story, onFirstPage=footer, onLaterPages=footer)


def main():
    PLOTS.mkdir(parents=True, exist_ok=True)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    df, digest = load_data()
    stats = summarize(df)
    stats.to_csv(ROOT / "analysis/summary_metrics.csv", index=False, float_format="%.3f")
    plt.rcParams.update({"font.family":"DejaVu Sans", "font.size":9, "axes.titlesize":11,
                         "axes.titleweight":"bold", "axes.labelsize":9, "path.simplify":False})
    rpm = [("raw_rpm","Raw RPM","#96a8b4",.65),("target_rpm","Target RPM","#d36b38",1.2),("filtered_rpm","Filtered RPM","#146680",1)]
    rate = [("rpm_rate","rpm_rate (до EMA)","#9babb8",.7),("filtered_rate","filtered_rate (після EMA)","#146680",1)]
    pid = [("p","P","#146680",.8),("i","I","#d36b38",1),("d","D","#8151a0",.8)]
    plot(df,"rpm_full","A. RPM: весь запис",rpm,"RPM")
    plot(df,"rpm_opening","A. RPM: перші 120 с",rpm,"RPM",True)
    plot(df,"rate_full","B. Похідна: весь запис",rate,"RPM/s")
    plot(df,"rate_opening","B. Похідна: перші 120 с",rate,"RPM/s",True)
    plot(df,"pid_full","C. Складові PID: весь запис",pid,"Відсоткові пункти")
    plot(df,"power_full","D. Команда PWM: весь запис",[("power","power (CSV)","#146680",.8)],"Команда, %",power=True)
    build_report(df, stats, digest)
    print(f"Created {REPORT}; {len(df)} rows, CSV SHA-256 {digest}")

if __name__ == "__main__":
    main()

