# The template now only represents an individual server block
server_template = """
    server {{
        listen 8080;
        server_name fahd{port}.com;

        root ./www/html;
        index index.html;
        timeout 80;
    }}
"""

# Adjust range to 8280 if you want exactly 200 servers (8480 will generate 400 servers)
start_port = 8080
end_port = 8100

with open("test_servers.conf", "w") as f:
    # 1. Open the main http context
    f.write("http {\n")
    f.write("    client_max_body_size 100M;\n")
    
    # 2. Generate and inject all the virtual servers inside it
    for p in range(start_port, end_port):
        f.write(server_template.format(port=p))
        
    # 3. Close the main http context
    f.write("}\n")

total_servers = 20
print(f"Created test_servers.conf with {total_servers} servers inside a single http context successfully!")