for port in {8080..8089}
do
    rm build/replica/chunks/$port/*
done