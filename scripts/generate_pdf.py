import markdown
import sys
from weasyprint import HTML

def md_to_pdf(input_md, output_pdf):
    with open(input_md, "r", encoding="utf-8") as f:
        md_text = f.read()

    html_content = markdown.markdown(md_text, extensions=['tables', 'fenced_code'])

    styled_html = f"""
    <!DOCTYPE html>
    <html lang="es">
    <head>
        <meta charset="utf-8">
        <style>
            @page {{
                size: letter;
                margin: 2.2cm 2cm 2.2cm 2cm;
                @bottom-right {{
                    content: "Página " counter(page) " de " counter(pages);
                    font-size: 9pt;
                    color: #666;
                }}
                @top-center {{
                    content: "Proyecto Final: Gestión de Procesos y Planificación de CPU - Sistemas Operativos";
                    font-size: 8pt;
                    color: #888;
                    border-bottom: 0.5pt solid #ccc;
                    padding-bottom: 4px;
                }}
            }}
            body {{
                font-family: 'Liberation Sans', 'Helvetica Neue', Helvetica, Arial, sans-serif;
                font-size: 10pt;
                line-height: 1.55;
                color: #222;
            }}
            h1 {{
                font-size: 18pt;
                color: #0d47a1;
                margin-top: 0;
                margin-bottom: 6px;
                border-bottom: 2pt solid #0d47a1;
                padding-bottom: 6px;
            }}
            h2 {{
                font-size: 13pt;
                color: #1565c0;
                margin-top: 18px;
                margin-bottom: 8px;
                border-bottom: 1pt solid #e0e0e0;
                padding-bottom: 3px;
                page-break-after: avoid;
            }}
            h3 {{
                font-size: 11pt;
                color: #283593;
                margin-top: 14px;
                margin-bottom: 6px;
                page-break-after: avoid;
            }}
            p, ul, ol {{
                margin-top: 6px;
                margin-bottom: 8px;
            }}
            li {{
                margin-bottom: 4px;
            }}
            table {{
                width: 100%;
                border-collapse: collapse;
                margin-top: 12px;
                margin-bottom: 16px;
                font-size: 8.5pt;
                page-break-inside: avoid;
            }}
            th, td {{
                border: 1px solid #cfd8dc;
                padding: 6px 8px;
                text-align: left;
            }}
            th {{
                background-color: #eceff1;
                color: #263238;
                font-weight: bold;
            }}
            tr:nth-child(even) {{
                background-color: #f8fafc;
            }}
            pre, code {{
                font-family: 'Liberation Mono', 'Courier New', Courier, monospace;
                background-color: #f5f5f5;
                border-radius: 3px;
            }}
            code {{
                padding: 1px 4px;
                font-size: 9pt;
            }}
            pre {{
                padding: 10px;
                border: 1px solid #e0e0e0;
                font-size: 8.5pt;
                white-space: pre-wrap;
                word-wrap: break-word;
            }}
            blockquote {{
                border-left: 3pt solid #1976d2;
                margin-left: 0;
                padding-left: 12px;
                color: #455a64;
                font-style: italic;
            }}
            hr {{
                border: none;
                border-top: 1pt solid #cfd8dc;
                margin: 16px 0;
            }}
        </style>
    </head>
    <body>
        {html_content}
    </body>
    </html>
    """

    HTML(string=styled_html).write_pdf(output_pdf)
    print(f"[+] PDF generado exitosamente: {output_pdf}")

if __name__ == "__main__":
    md_file = sys.argv[1] if len(sys.argv) > 1 else "docs/informe_tecnico.md"
    pdf_file = sys.argv[2] if len(sys.argv) > 2 else "docs/informe_tecnico.pdf"
    md_to_pdf(md_file, pdf_file)
