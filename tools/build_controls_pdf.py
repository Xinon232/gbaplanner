#!/usr/bin/env python3
"""Builds gbaplanner-full-controls.pdf from the maintained README: every
screen's controls and the calendar.txt / todo.txt formats (like gbawriter's
and gbavocab's tools/build_controls_pdf.py). Needs ReportLab and the DejaVu
Sans fonts (/usr/share/fonts/truetype/dejavu).

Usage: python3 tools/build_controls_pdf.py [OUTPUT.pdf]
"""
from pathlib import Path
import re
import sys
from xml.sax.saxutils import escape

from reportlab import rl_config
rl_config.invariant = 1   # reproducible output
from reportlab.lib import colors  # noqa: E402
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle  # noqa: E402
from reportlab.pdfbase import pdfmetrics  # noqa: E402
from reportlab.pdfbase.ttfonts import TTFont  # noqa: E402
from reportlab.platypus import Flowable, Paragraph, Preformatted, SimpleDocTemplate, Spacer, Table, TableStyle  # noqa: E402

root = Path(__file__).resolve().parents[1]
output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'gbaplanner-full-controls.pdf'
fontdir = Path('/usr/share/fonts/truetype/dejavu')
for name, filename in [('Body', 'DejaVuSans.ttf'), ('BodyBold', 'DejaVuSans-Bold.ttf'), ('Mono', 'DejaVuSansMono.ttf')]:
    pdfmetrics.registerFont(TTFont(name, str(fontdir / filename)))
pdfmetrics.registerFontFamily('Body', normal='Body', bold='BodyBold', italic='Body', boldItalic='BodyBold')
BLUE = colors.HexColor('#174a8b')
styles = getSampleStyleSheet()
styles.add(ParagraphStyle(name='BodyTextUTF', fontName='Body', fontSize=9, leading=13, spaceAfter=7))
styles.add(ParagraphStyle(name='SectionUTF', fontName='BodyBold', fontSize=13, leading=17, textColor=BLUE,
                          spaceBefore=12, spaceAfter=8, keepWithNext=True))
styles.add(ParagraphStyle(name='SubUTF', fontName='BodyBold', fontSize=10.5, leading=14, textColor=BLUE,
                          spaceBefore=8, spaceAfter=6, keepWithNext=True))
styles.add(ParagraphStyle(name='TitleUTF', fontName='BodyBold', fontSize=23, leading=28, textColor=BLUE, spaceAfter=14))
styles.add(ParagraphStyle(name='CellUTF', fontName='Body', fontSize=8, leading=11))
styles.add(ParagraphStyle(name='CodeUTF', fontName='Mono', fontSize=7, leading=9.5, leftIndent=6,
                          backColor=colors.HexColor('#f3f6fa'), borderPadding=4, spaceAfter=9))


def markup(text):
    text = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', text)
    text = escape(text)
    text = re.sub(r'\*\*(.*?)\*\*', r'<b>\1</b>', text)
    return re.sub(r'`([^`]*)`', r'<font name="Mono">\1</font>', text)


readme = (root / 'README.md').read_text(encoding='utf-8')
version = re.search(r'^# gbaplanner (\d+\.\d+)$', readme, re.M).group(1)
# Everything a player needs: files and formats, every screen, settings, limits.
body = readme.split('## Flashcarts\n', 1)[1].split('## Building', 1)[0]
story: list[Flowable] = [
    Paragraph(f'gbaplanner {version}', styles['TitleUTF']),
    Paragraph('Full controls · Halim Jarrar', styles['SectionUTF']),
    Paragraph('A calendar and to-do list for the Game Boy Advance: months with a coloured dot for every item, a day '
              'view, a to-do list, country holidays for 34 countries, repeating entries and notes, saved on the SD '
              'card as plain text files (calendar.txt, todo.txt) or in the app\'s own store. No real-time clock is '
              'used.', styles['BodyTextUTF']),
    Paragraph('Hardware-unverified build: emulator and host tests are not proof of safe saving on a physical cart. '
              'Back up your SD card and try it with copies of your files first.', styles['BodyTextUTF']),
    Paragraph('Flashcarts', styles['SectionUTF'])]
lines = body.splitlines()
i = 0
while i < len(lines):
    raw = lines[i]
    line = raw.strip()
    if not line:
        i += 1
        continue
    if line.startswith('```'):
        block = []
        i += 1
        while i < len(lines) and not lines[i].strip().startswith('```'):
            block.append(lines[i])
            i += 1
        story.append(Preformatted('\n'.join(block), styles['CodeUTF']))
        i += 1
        continue
    if line.startswith('|'):
        rows = []
        while i < len(lines) and lines[i].strip().startswith('|'):
            row = [c.strip() for c in re.split(r'(?<!\\)\|', lines[i].strip().strip('|'))]
            if not all(re.fullmatch(r'[-:]+', c) for c in row):
                rows.append([Paragraph(markup(c), styles['CellUTF']) for c in row])
            i += 1
        widths = [150, 325] if len(rows[0]) == 2 else [475 // len(rows[0])] * len(rows[0])
        table = Table(rows, colWidths=widths, repeatRows=1, hAlign='LEFT')
        table.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#eaf1fa')),
                                   ('VALIGN', (0, 0), (-1, -1), 'TOP'),
                                   ('LINEBELOW', (0, 0), (-1, 0), 0.6, BLUE),
                                   ('LEFTPADDING', (0, 0), (-1, -1), 6), ('RIGHTPADDING', (0, 0), (-1, -1), 6),
                                   ('TOPPADDING', (0, 0), (-1, -1), 5), ('BOTTOMPADDING', (0, 0), (-1, -1), 5)]))
        story.extend([table, Spacer(1, 9)])
        continue
    if line.startswith('### '):
        story.append(Paragraph(markup(line[4:]), styles['SubUTF']))
    elif line.startswith('#'):
        story.append(Paragraph(markup(line.lstrip('#').strip()), styles['SectionUTF']))
    else:
        if line.startswith('- '):
            line = '• ' + line[2:]
        if line.startswith('> '):
            line = line[2:]
        story.append(Paragraph(markup(line), styles['BodyTextUTF']))
    i += 1
credits = readme.split('## About / license\n', 1)[1].strip().split('\n\n')
story.append(Paragraph('About / license', styles['SectionUTF']))
for para in credits:
    story.append(Paragraph(markup(para), styles['BodyTextUTF']))


def footer(canvas, doc):
    canvas.setFont('Body', 8)
    canvas.setFillColor(colors.HexColor('#526174'))
    canvas.drawString(60, 30, f'gbaplanner {version} · Full controls · Halim Jarrar')
    canvas.drawRightString(535, 30, str(doc.page))


SimpleDocTemplate(str(output), pagesize=(595.28, 841.89), rightMargin=60, leftMargin=60, topMargin=48,
                  bottomMargin=52, title=f'gbaplanner {version} — Full controls',
                  author='Halim Jarrar').build(story, onFirstPage=footer, onLaterPages=footer)
print(output)
