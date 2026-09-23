"""Optional build wrapper for networks whose package mirrors stall indefinitely.

Uses the normal PlatformIO CLI with a finite read timeout. Does not change packages,
versions, firmware code, or the standard `pio run` build path.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading
import time
import requests
import platformio.http
from platformio.package.download import FileDownloader
from platformio.__main__ import main

platformio.http.__default_requests_timeout__ = (15, 45)
original_start=FileDownloader.start

def ranged_start(self,with_progress=True,silent=False):
    size=self.get_size()
    if size<1024*1024:
        return original_start(self,with_progress=with_progress,silent=silent)
    url=self._http_response.url
    self._http_response.close()
    chunk=2*1024*1024
    lock=threading.Lock()
    with open(self.get_filepath(),"w+b") as output:
        output.truncate(size)
        def fetch(start):
            end=min(size-1,start+chunk-1)
            for attempt in range(5):
                try:
                    response=requests.get(url,headers={"Range":f"bytes={start}-{end}"},timeout=(15,45))
                    response.raise_for_status()
                    if response.status_code!=206 or response.headers.get("Content-Range")!=f"bytes {start}-{end}/{size}" or len(response.content)!=end-start+1:
                        raise IOError("Mirror did not return the requested byte range")
                    with lock:
                        output.seek(start);output.write(response.content)
                    return len(response.content)
                except (requests.RequestException,IOError):
                    if attempt==4:raise
                    time.sleep(attempt+1)
        completed=0;next_report=10
        print(f"Downloading {size//1048576} MiB in verified ranges",flush=True)
        with ThreadPoolExecutor(max_workers=12) as executor:
            futures=[executor.submit(fetch,start) for start in range(0,size,chunk)]
            for future in as_completed(futures):
                completed+=future.result()
                while completed*100/size>=next_report:
                    print(f"Downloading {next_report}%",flush=True);next_report+=10
    # PlatformIO performs its normal registry SHA256 verification after this method.
    return True

FileDownloader.start=ranged_start
if __name__ == "__main__":
    raise SystemExit(main())
