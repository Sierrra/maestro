FROM postgres:9.4
RUN localedef -i ru_RU -c -f UTF-8 -A /usr/share/locale/locale.alias ru_RU.UTF-8
ENV LANG ru_RU.utf8
ENV POSTGRES_USER=docker
ENV POSTGRES_PASSWORD=dockerpass
ENV POSTGRES_DB=metadata
#VOLUME ./init.sql:/docker-entrypoint-initdb.d/
ADD ./init.sql/docker-entrypoint-initdb.d/metadata_.sql /docker-entrypoint-initdb.d/metadata_.sql
ADD ./init.sql/docker-entrypoint-initdb.d/entry.sh /docker-entrypoint-initdb.d/init.sh