FROM nginx:alpine
RUN apk --update add  \
      npm \
      yarn 
RUN mkdir /app
ADD ./maestro /app/
WORKDIR /app
RUN npm install
RUN npm run build
RUN cp -r /app/build /var/www/
EXPOSE 443:443
