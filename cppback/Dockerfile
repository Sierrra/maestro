FROM gcc:latest as build

RUN apt-get update && \
    apt-get install -y \
      libpoco-dev\
      git \
      cmake \ 
      make \
      libsndfile-dev

RUN mkdir /app
WORKDIR /app/

RUN git clone https://github.com/acl-dev/acl.git \ 
&& cd acl && cd lib_acl; make \
&& cd ../lib_protocol; make \
&& cd ../lib_acl_cpp; make
ADD ./handler.cpp /app/
# Установим рабочую директорию для сборки проекта

# Выполним сборку нашего проекта, а также его тестирование
RUN g++ -std=c++14 -o main handler.cpp -lsndfile \
    -lPocoUtil -lPocoNet -lPocoFoundation -lPocoJSON -I/app/acl/lib_acl_cpp/include/ \
    -L /app/acl/lib_acl_cpp/lib/ -l_acl_cpp -L /app/acl/lib_protocol/lib/ \
    -l_protocol -L /app/acl/lib_acl/lib/ -l_acl -lpthread

CMD ["/app/main"]