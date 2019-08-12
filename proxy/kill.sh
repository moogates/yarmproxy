ps axu | grep mcproxy$ | awk '{print $2}'
ps axu | grep mcproxy$ | awk '{print $2}' | xargs kill -9
