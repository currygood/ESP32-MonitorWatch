# -*- coding: utf-8 -*-
"""Generate signal-processing & seizure-AI pipeline diagrams (v2).
Writes .drawio (editable in draw.io) plus SVG previews; PNG via headless Chrome.
"""
import pathlib

HERE = pathlib.Path(__file__).resolve().parent

PALETTE = {
    "sensor":  ("#f5f5f5", "#666666"),
    "filter":  ("#e8f0fe", "#6c8ebf"),
    "model":   ("#d5e8d4", "#82b366"),
    "decisor": ("#fff2cc", "#d6b656"),
    "output":  ("#ffe6cc", "#d79b00"),
    "alarm":   ("#f8cecc", "#b85450"),
}

def N(cid, label, x, y, w, h, role="filter", kind="rect", font=12):
    return dict(id=cid, label=label, x=x, y=y, w=w, h=h, role=role, kind=kind, font=font)

def xml_attr(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
             .replace('"', "&quot;").replace("\n", "&#xa;"))

def build_drawio(name, nodes, edges, width, height):
    P = []
    P.append('<?xml version="1.0" encoding="UTF-8"?>')
    P.append('<mxfile host="app.diagrams.net" version="26.0.0">')
    P.append('  <diagram id="d1" name="%s">' % name)
    P.append('    <mxGraphModel dx="950" dy="550" grid="1" gridSize="10" guides="1" tooltips="1" '
             'connect="1" arrows="1" fold="1" page="1" pageScale="1" pageWidth="%d" pageHeight="%d" '
             'math="0" shadow="0">' % (width, height))
    P.append('      <root>')
    P.append('        <mxCell id="0" />')
    P.append('        <mxCell id="1" parent="0" />')
    for n in nodes:
        fill, stroke = PALETTE[n["role"]]
        val = xml_attr(n["label"])
        if n["kind"] == "diamond":
            style = "rhombus;whiteSpace=wrap;html=1;fillColor=%s;strokeColor=%s;" % (fill, stroke)
        else:
            style = "rounded=1;whiteSpace=wrap;html=1;fillColor=%s;strokeColor=%s" % (fill, stroke)
        P.append('        <mxCell id="%s" value="%s" style="%s" vertex="1" parent="1">' % (n["id"], val, style))
        P.append('          <mxGeometry x="%d" y="%d" width="%d" height="%d" as="geometry" />'
                 % (n["x"], n["y"], n["w"], n["h"]))
        P.append('        </mxCell>')
    for e in edges:
        st = ["edgeStyle=orthogonalEdgeStyle", "rounded=1", "orthogonalLoop=1",
              "jettySize=auto", "html=1"]
        if e.get("dashed"):
            st.append("dashed=1")
        if e.get("exit"):
            st.append("exitX=%s;exitY=%s;exitDx=0;exitDy=0" % (e["exit"][0], e["exit"][1]))
        if e.get("entry"):
            st.append("entryX=%s;entryY=%s;entryDx=0;entryDy=0" % (e["entry"][0], e["entry"][1]))
        lab = (' value="%s"' % xml_attr(e["label"])) if e.get("label") else ""
        P.append('        <mxCell id="%s"%s style="%s" edge="1" parent="1" source="%s" target="%s">'
                 % (e["id"], lab, ";".join(st), e["src"], e["dst"]))
        P.append('          <mxGeometry relative="1" as="geometry">')
        if e.get("wpts"):
            P.append('            <Array as="points">')
            for wx, wy in e["wpts"]:
                P.append('              <mxPoint x="%d" y="%d" />' % (wx, wy))
            P.append('            </Array>')
        P.append('          </mxGeometry>')
        P.append('        </mxCell>')
    P.append('      </root>')
    P.append('    </mxGraphModel>')
    P.append('  </diagram>')
    P.append('</mxfile>')
    return "\n".join(P)

def svg_polyline(pts, dashed):
    d = []
    for i, (x, y) in enumerate(pts):
        d.append(("%d,%d" % (x, y)) if i == 0 else ("%d,%d" % (x, y)))
    dd = ' stroke-dasharray="7,6"' if dashed else ""
    return ('<polyline points="' + " ".join(d) + '" fill="none" stroke="#555" stroke-width="1.7" '
            'marker-end="url(#arr)"' + dd + " />")

def build_svg(nodes, edges, width, height, title):
    P = []
    P.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
             'font-family="Segoe UI, Microsoft YaHei, PingFang SC, sans-serif">'
             % (width, height, width, height))
    P.append('<defs>'
             '<marker id="arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
             'markerHeight="7" orient="auto-start-reverse">'
             '<path d="M 0 0 L 10 5 L 0 10 z" fill="#555"/></marker></defs>')
    if title:
        P.append('<text x="%d" y="30" text-anchor="middle" font-size="18" font-weight="bold" '
                 'fill="#333">%s</text>' % (width // 2, title))
    box = {n["id"]: n for n in nodes}

    def ctc(nid, side):
        n = box[nid]
        if side == "l":
            return n["x"], n["y"] + n["h"] / 2
        if side == "r":
            return n["x"] + n["w"], n["y"] + n["h"] / 2
        if side == "b":
            return n["x"] + n["w"] / 2, n["y"] + n["h"]
        return n["x"] + n["w"] / 2, n["y"]

    for e in edges:
        ax, ay = ctc(e["src"], e.get("sf", "r"))
        bx, by = ctc(e["dst"], e.get("tf", "l"))
        if e.get("wpts"):
            pts = [(ax, ay)] + list(e["wpts"]) + [(bx, by)]
            P.append(svg_polyline(pts, e.get("dashed")))
        else:
            dash = ' stroke-dasharray="7,6"' if e.get("dashed") else ""
            P.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="#555" stroke-width="1.7" '
                     'marker-end="url(#arr)"%s />' % (ax, ay, bx, by, dash))
        if e.get("label"):
            if e.get("wpts"):
                lx, ly = e["wpts"][0][0] + 60, e["wpts"][0][1] - 8
            else:
                lx, ly = (ax + bx) / 2, (ay + by) / 2
                if e.get("lpos"):
                    lx, ly = e["lpos"]
            P.append('<rect x="%d" y="%d" width="150" height="20" rx="4" fill="#fff" opacity="0.9" '
                     'stroke="#ccc"/><text x="%d" y="%d" text-anchor="middle" font-size="11" '
                     'fill="#222">%s</text>' % (lx - 75, ly - 16, lx, ly, e["label"]))
    for n in nodes:
        x, y, w, h = n["x"], n["y"], n["w"], n["h"]
        fill, stroke = PALETTE[n["role"]]
        if n["kind"] == "diamond":
            P.append('<polygon points="%d,%d %d,%d %d,%d %d,%d" fill="%s" stroke="%s" stroke-width="1.7"/>'
                     % (x + w / 2, y, x + w, y + h / 2, x + w / 2, y + h, x, y + h / 2, fill, stroke))
            lx, ly = x + w / 2, y + h / 2
        else:
            P.append('<rect x="%d" y="%d" width="%d" height="%d" rx="9" fill="%s" stroke="%s" '
                     'stroke-width="1.7"/>' % (x, y, w, h, fill, stroke))
            lx, ly = x + w / 2, y + h / 2
        lines_arr = n["label"].split("\n")
        fs = n["font"]
        lh = 15 if fs == 11 else 17
        start = ly - (len(lines_arr) - 1) * lh / 2 + 2
        for i, ln in enumerate(lines_arr):
            P.append('<text x="%d" y="%d" text-anchor="middle" font-size="%d" fill="#202020">%s</text>'
                     % (lx, start + i * lh, fs, ln))
    P.append('</svg>')
    return "\n".join(P)

# ---------------- diagram 1: signal processing / fusion ----------------
N1 = [
    N("n1", "MPU6050\n六轴采样 50Hz", 40, 80, 190, 66, role="sensor"),
    N("n2", "硬件 DLPF\nACC 21Hz / GYRO 20Hz", 300, 80, 200, 66),
    N("n3", "单点尖峰抑制\n±1g / ±61°/s", 570, 80, 190, 66),
    N("n4", "EMA 一阶低通\nα=0.40  fc≈4.1Hz", 830, 80, 190, 66),
    N("n5", "运动指数\n滤波后加速度", 1070, 80, 170, 66),
    N("p1", "MAX30102 采样 50Hz\nIR / RED / 温度", 40, 240, 190, 66, role="sensor"),
    N("p2", "PPG IIR 带通\n0.5~5Hz", 300, 240, 190, 66),
    N("p3", "峰值检测\n提取原始心率", 570, 240, 190, 66),
    N("p4", "突变确认\n连续 3 次才采信", 830, 240, 190, 66),
    N("p5", "原始心率", 1070, 240, 170, 66),
    N("s1", "R 比值\n5 点中位数", 300, 400, 190, 66),
    N("s2", "SpO2 饱和度\n查表", 570, 400, 190, 66),
    N("s3", "温度 temp", 830, 400, 190, 66),
    N("f1", "SignalFusion 卡尔曼融合\nQ=0.5, R=9+60*motion\nmotion>=1 冻结\n连续 8 帧无效置无效\nHR 40~200 bpm", 1330, 140, 260, 190, role="model"),
    N("f2", "特征窗口 200x7\n(10s @ 20Hz)", 1660, 140, 200, 190, role="model"),
]
E1 = [
    {"id": "e1", "src": "n1", "dst": "n2"},
    {"id": "e2", "src": "n2", "dst": "n3"},
    {"id": "e3", "src": "n3", "dst": "n4"},
    {"id": "e4", "src": "n4", "dst": "n5"},
    {"id": "e5", "src": "n5", "dst": "f1"},
    {"id": "e6", "src": "p1", "dst": "p2"},
    {"id": "e7", "src": "p2", "dst": "p3"},
    {"id": "e8", "src": "p3", "dst": "p4"},
    {"id": "e9", "src": "p4", "dst": "p5"},
    {"id": "e10", "src": "p5", "dst": "f1"},
    {"id": "e11", "src": "p1", "dst": "s1", "dashed": True, "sf": "b", "tf": "t",
     "wpts": [(135, 400), (135, 560), (395, 560), (395, 400)]},
    {"id": "e12", "src": "s1", "dst": "s2"},
    {"id": "e13", "src": "s2", "dst": "f1", "sf": "b", "tf": "b",
     "wpts": [(665, 466), (665, 530), (1330, 530), (1330, 330)]},
    {"id": "e14", "src": "p1", "dst": "s3", "dashed": True, "sf": "b", "tf": "t"},
    {"id": "e15", "src": "s3", "dst": "f1", "sf": "b", "tf": "b"},
    {"id": "e16", "src": "f1", "dst": "f2"},
]

# ---------------- diagram 2 : seizure model ----------------
N2 = [
    N("m1", "特征窗口 200x7\n10s @ 20Hz 滑动", 40, 170, 200, 90, role="sensor"),
    N("m2", "z-score 归一化\nmean/std <- JSON", 320, 170, 200, 90),
    N("m3", "TFLite-Micro int8 CNN\nConv2D->MaxPool->GAP->FC->Sigmoid\n8.4KB / SPIFFS model 分区", 600, 140, 260, 150, role="model"),
    N("m4", "输出概率 P in [0,1]", 940, 170, 190, 90, role="model"),
    N("m5", "P>=0.81 ?\n或 SpO2 低于 90%", 1210, 150, 210, 120, role="decisor", kind="diamond"),
    N("m6", "风险等级 0~100\nRound(P*100)\nSpO2 低于 90 强制 >=90", 1500, 140, 230, 120, role="output"),
    N("m7", "MQTT / OneNET\nabnormal_motion_detected\nheart_rate\noxygen_saturation\nseizure_risk_level", 1500, 330, 230, 135, role="alarm"),
]
E2 = [
    {"id": "x1", "src": "m1", "dst": "m2"},
    {"id": "x2", "src": "m2", "dst": "m3"},
    {"id": "x3", "src": "m3", "dst": "m4"},
    {"id": "x4", "src": "m4", "dst": "m5"},
    {"id": "x5", "src": "m5", "dst": "m6", "label": "是", "sf": "b", "tf": "t",
     "wpts": [(1315, 270), (1315, 130), (1615, 130), (1615, 140)]},
    {"id": "x6", "src": "m6", "dst": "m7"},
    {"id": "x7", "src": "m5", "dst": "m1", "dashed": True, "label": "否 -> 下一窗口", "sf": "t", "tf": "t",
     "wpts": [(1315, 150), (1315, 105), (115, 105), (115, 170)]},
]

if __name__ == "__main__":
    (HERE / "signal_processing_pipeline.drawio").write_text(
        build_drawio("信号滤波与融合链路", N1, E1, 1900, 700), encoding="utf-8")
    (HERE / "seizure_model_pipeline.drawio").write_text(
        build_drawio("端侧癫痫AI推理链路", N2, E2, 1800, 560), encoding="utf-8")
    (HERE / "signal_processing_pipeline.svg").write_text(
        build_svg(N1, E1, 1900, 700, "信号采集 -> 滤波 -> 融合 -> 特征窗口"), encoding="utf-8")
    (HERE / "seizure_model_pipeline.svg").write_text(
        build_svg(N2, E2, 1800, 560, "端侧癫痫 AI 模型（每 10s 窗口推理）"), encoding="utf-8")
    print("OK v2")
