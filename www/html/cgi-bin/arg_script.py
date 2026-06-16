#!/usr/bin/env python3
import sys
import html

def main():
    # 1. Print the required CGI HTTP Header
    # Crucial: The blank line after Content-Type tells the server the header is done.
    print("Content-Type: text/html; charset=utf-8")
    print()

    # 2. Start the HTML Document
    print("<!DOCTYPE html>")
    print("<html>")
    print("<head>")
    print("    <title>CGI Arguments Viewer</title>")
    print("    <style>")
    print("        body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f6f9; }")
    print("        .container { max-width: 600px; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }")
    print("        h1 { color: #333; }")
    print("        .alert { padding: 15px; background-color: #ff9800; color: white; border-radius: 4px; font-weight: bold; }")
    print("        table { width: 100%; border-collapse: collapse; margin-top: 20px; }")
    print("        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }")
    print("        th { background-color: #007bff; color: white; }")
    print("        tr:hover { background-color: #f5f5f5; }")
    print("    </style>")
    print("</head>")
    print("<body>")
    print("    <div class='container'>")

    # sys.argv[0] is the script name itself. 
    # sys.argv[1:] contains the actual arguments passed to the script.
    args = sys.argv[1:]
    num_args = len(args)

    # 3. Check arguments and render conditional content
    if num_args == 0:
        print("        <h1>CGI Assessment</h1>")
        print("        <div class='alert'>No arguments were provided to this script.</div>")
    else:
        print(f"        <h1>Received Arguments</h1>")
        print(f"        <p><strong>Total Count:</strong> {num_args}</p>")
        
        # Render the HTML Table
        print("        <table>")
        print("            <thead>")
        print("                <tr>")
        print("                    <th>Index</th>")
        print("                    <th>Argument Value</th>")
        print("                </tr>")
        print("            </thead>")
        print("            <tbody>")
        
        for index, arg in enumerate(args, start=1):
            # html.escape prevents Cross-Site Scripting (XSS) if someone passes malicious code
            safe_arg = html.escape(arg)
            print("                <tr>")
            print(f"                    <td>{index}</td>")
            print(f"                    <td>{safe_arg}</td>")
            print("                </tr>")
            
        print("            </tbody>")
        print("        </table>")

    # 4. Close HTML Tags
    print("    </div>")
    print("</body>")
    print("</html>")

if __name__ == "__main__":
    main()
