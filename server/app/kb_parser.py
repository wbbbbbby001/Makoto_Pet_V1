# -*- coding: utf-8 -*-
"""知识库文档解析：PDF / Word(.docx) / Markdown / TXT。"""
import os
import re


def _clean(text: str) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = re.sub(r"[ \t]+\n", "\n", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp", ".bmp"}
IMAGE_MIME = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
              ".webp": "image/webp", ".bmp": "image/bmp"}


def _vision_pdf(path: str, progress, vision) -> str:
    try:
        try:
            import pymupdf as fitz
        except ImportError:
            import fitz
    except ImportError as e:
        raise RuntimeError("扫描版 PDF 需要 pymupdf：请运行 run.bat 安装依赖") from e

    doc = fitz.open(path)
    pages = len(doc)
    parts = []
    errors = []
    try:
        for i, page in enumerate(doc):
            try:
                pix = page.get_pixmap(dpi=150)
                text = vision.transcribe(pix.tobytes("png"), "image/png")
                if text and text.strip() not in ("无文字", "無文字"):
                    parts.append(text.strip())
            except Exception as e:
                errors.append("第 %d 页: %s" % (i + 1, e))
            if progress and pages:
                progress(10 + int(75 * (i + 1) / pages),
                         "视觉识别 PDF %d/%d 页" % (i + 1, pages))
    finally:
        doc.close()

    if not parts and errors:
        raise RuntimeError("视觉识别失败：" + errors[0])
    if not parts:
        raise RuntimeError("PDF 没有识别出文字")
    if progress:
        progress(85, "整理文本")
    return _clean("\n".join(parts))


def parse_file(path: str, ext: str, progress=None, vision=None) -> str:
    """返回纯文本。progress(percent_0_100, message) 可在后台线程里被调用。"""
    ext = (ext or "").lower()

    if ext in IMAGE_EXTS:
        if vision is None or not vision.configured:
            raise RuntimeError("图片解析需要配置视觉模型（vision.enabled / api_key）")
        if progress:
            progress(20, "识别图片文字")
        with open(path, "rb") as f:
            data = f.read()
        text = vision.transcribe(data, IMAGE_MIME.get(ext, "image/png"))
        if progress:
            progress(85, "整理文本")
        if not text or text.strip() in ("无文字", "無文字"):
            raise RuntimeError("图片里没有识别出文字")
        return _clean(text)

    if ext in (".md", ".markdown", ".txt"):
        if progress:
            progress(20, "读取文档")
        raw = open(path, "rb").read()
        for enc in ("utf-8", "utf-8-sig", "gb18030"):
            try:
                text = raw.decode(enc)
                break
            except UnicodeDecodeError:
                continue
        else:
            text = raw.decode("utf-8", "ignore")
        if progress:
            progress(85, "整理文本")
        return _clean(text)

    if ext == ".pdf":
        try:
            from pypdf import PdfReader
        except ImportError as e:
            raise RuntimeError("缺少 pypdf，请先运行 run.bat 安装依赖") from e
        reader = PdfReader(path)
        pages = len(reader.pages)
        parts = []
        for i, page in enumerate(reader.pages):
            try:
                parts.append(page.extract_text() or "")
            except Exception:
                parts.append("")
            if progress and pages:
                progress(10 + int(70 * (i + 1) / pages),
                         "解析 PDF %d/%d 页" % (i + 1, pages))
        text = "\n".join(parts)
        if len(text.strip()) >= 20:
            if progress:
                progress(85, "整理文本")
            return _clean(text)

        # 没有文字层：扫描版/图片版，逐页渲染后交给视觉模型
        if vision is None or not vision.configured:
            raise RuntimeError("PDF 没有文字层（扫描版），需要配置 vision 才能识别")
        return _vision_pdf(path, progress, vision)

    if ext == ".docx":
        try:
            import docx
        except ImportError as e:
            raise RuntimeError("缺少 python-docx，请先运行 run.bat 安装依赖") from e
        doc = docx.Document(path)
        parts = []
        total = max(1, len(doc.paragraphs))
        for i, p in enumerate(doc.paragraphs):
            if p.text.strip():
                parts.append(p.text)
            if progress and i % 20 == 0:
                progress(10 + int(70 * i / total), "解析 Word %d/%d" % (i, total))
        for table in doc.tables:
            for row in table.rows:
                cells = [c.text.strip() for c in row.cells if c.text.strip()]
                if cells:
                    parts.append(" | ".join(cells))
        if progress:
            progress(85, "整理文本")
        return _clean("\n".join(parts))

    if ext == ".doc":
        raise RuntimeError("暂不支持旧版 .doc，请另存为 .docx 后再上传")

    raise RuntimeError("不支持的文件类型：%s（支持 PDF / .docx / .md / .txt）" % ext)


def chunk_text(text: str, max_len: int = 1200, max_chunks: int = 80) -> list:
    """按段落聚合成适合检索的块。"""
    text = _clean(text)
    if not text:
        return []

    paras = [p.strip() for p in re.split(r"\n\s*\n", text) if p.strip()]
    chunks = []
    buf = ""
    for p in paras:
        while len(p) > max_len:
            if buf:
                chunks.append(buf)
                buf = ""
            cut = p.rfind("。", 0, max_len)
            if cut < max_len * 0.3:
                cut = p.rfind("\n", 0, max_len)
            if cut < max_len * 0.3:
                cut = max_len
            chunks.append(p[:cut + 1].strip())
            p = p[cut + 1:].strip()
            if len(chunks) >= max_chunks:
                return chunks[:max_chunks]
        if len(buf) + len(p) + 1 <= max_len:
            buf = (buf + "\n" + p).strip() if buf else p
        else:
            chunks.append(buf)
            buf = p
            if len(chunks) >= max_chunks:
                return chunks[:max_chunks]
    if buf:
        chunks.append(buf)
    return chunks[:max_chunks]
