import asyncio
import aiohttp
import os
import sys
import time
import argparse

async def chunk_generator(data, chunk_size=2):
    """Asynchronously slices the data into 2-byte blocks."""
    for i in range(0, len(data), chunk_size):
        yield data[i:i + chunk_size]
        await asyncio.sleep(0.001) 

async def send_throttled_post(session, url, payload, worker_id, request_num):
    """Sends a chunked POST request with exactly 2-byte body blocks."""
    headers = {
        "Content-Type": "application/octet-stream"
    }
    
    start_time = time.time()
    try:
        async with session.post(url, data=chunk_generator(payload), headers=headers) as response:
            await response.text()
            duration = time.time() - start_time
            return {
                "worker": worker_id,
                "req": request_num,
                "status": response.status,
                "duration": duration,
                "error": None
            }
    except Exception as e:
        duration = time.time() - start_time
        return {
            "worker": worker_id,
            "req": request_num,
            "status": None,
            "duration": duration,
            "error": str(e)
        }

async def worker_loop(session, url, payload, worker_id, req_count, results_list):
    """A single worker running requests sequentially."""
    for i in range(1, req_count + 1):
        res = await send_throttled_post(session, url, payload, worker_id, i)
        results_list.append(res)
        
        if res["status"] == 200:
            print(f"✅ Worker {worker_id:03d} | Req {i} | Status: 200 | Time: {res['duration']:.2f}s")
        elif res["status"] is not None:
            print(f"❌ Worker {worker_id:03d} | Req {i} | Status: {res['status']} | Time: {res['duration']:.2f}s")
        else:
            print(f"❌ Worker {worker_id:03d} | Req {i} | Connection Lost! | Err: {res['error']}")

async def main():
    # --- COMMAND LINE ARGUMENTS PARSER ---
    parser = argparse.ArgumentParser(description="Advanced Asynchronous DDoS Simulation & Stress Tester for webserv")
    
    parser.add_argument("-u", "--url", default="http://localhost:5500/directory/youpla.bla", 
                        help="Target URL endpoint to hit (default: %(default)s)")
    parser.add_argument("-w", "--workers", type=int, default=100, 
                        help="Number of concurrent workers/connections (default: %(default)s)")
    parser.add_argument("-r", "--requests", type=int, default=5, 
                        help="Number of sequential requests per worker (default: %(default)s)")
    parser.add_argument("-f", "--file", default="100post.txt", 
                        help="Payload file to stream to the server (default: %(default)s)")
    
    args = parser.parse_args()

    # Verify payload file exists
    if not os.path.exists(args.file):
        print(f"⚠️ Warning: Base file '{args.file}' not found. Generating a temporary 1KB memory payload.")
        raw_payload = b"A" * 1024
    else:
        with open(args.file, "rb") as f:
            raw_payload = f.read()

    print("\n" + "="*50)
    print(f"🚀 LAUNCHING STRESS TEST")
    print(f"🔗 Target URL : {args.url}")
    print(f"🔥 Concurrency: {args.workers} Parallel Workers")
    print(f"🔄 Volume     : {args.requests} Requests Per Worker")
    print(f"📦 Total Load : {args.workers * args.requests} Total Transmissions")
    print(f"📄 Payload    : {args.file} ({len(raw_payload)} bytes)")
    print("="*50 + "\n")
    
    results = []
    connector = aiohttp.TCPConnector(limit=args.workers, ttl_dns_cache=300)
    
    async with aiohttp.ClientSession(connector=connector) as session:
        tasks = [worker_loop(session, args.url, raw_payload, w_id, args.requests, results) 
                 for w_id in range(1, args.workers + 1)]
        await asyncio.gather(*tasks)
        
    # --- METRIC ANALYSIS ---
    print("\n" + "="*50)
    print("📊 FINAL TEST METRICS SUMMARY")
    print("="*50)
    
    total_runs = len(results)
    statuses = [r["status"] for r in results if r["status"] is not None]
    errors = [r["error"] for r in results if r["error"] is not None]
    durations = [r["duration"] for r in results]
    
    success_count = statuses.count(200)
    
    print(f"Total Attempted Requests : {total_runs}")
    print(f"Successful HTTP 200     : {success_count} / {total_runs} ({ (success_count/total_runs)*100 if total_runs else 0 :.1f}%)")
    if errors:
        print(f"Network/Timeout Drops   : {len(errors)}")
        print(f"First Error Reason      : {errors[0]}")
    if durations:
        print(f"Average Roundtrip Time  : {sum(durations)/len(durations):.2f}s")
        print(f"Max Longest Connection  : {max(durations):.2f}s")
    print("="*50)

if __name__ == "__main__":
    if sys.platform >= "win32":
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    asyncio.run(main())