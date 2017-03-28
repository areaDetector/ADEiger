from flask import Flask, request

app = Flask(__name__)


@app.route("/<anything>/<anything2>/<anything3>/<anything4>/<anything5>/<anything6>", methods=["GET", "POST", "PUT"])
@app.route("/<anything>/<anything2>/<anything3>/<anything4>/<anything5>", methods=["GET", "POST", "PUT"])
@app.route("/<anything>/<anything2>/<anything3>/<anything4>", methods=["GET", "POST", "PUT"])
@app.route("/<anything>/<anything2>/<anything3>", methods=["GET", "POST", "PUT"])
@app.route("/<anything>/<anything2>", methods=["GET", "POST", "PUT"])
@app.route("/<anything>", methods=["GET", "POST", "PUT"])
def handle_any_request(*args, **kwargs):
    print("\n")
    print(request)
    if request.json:
        print(request.json)
    return "Hello, IOC! I am an Eiger detector, promise."


def main():
    app.run(host="127.0.0.1", port="5000")

if __name__ == '__main__':
    main()
