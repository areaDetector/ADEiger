from flask import Flask
app = Flask(__name__)


@app.route("/")
def hello():
    return "Hello, IOC! I am an Eiger detector, promise."


def main():
    app.run(host="127.0.0.1", port="5000")

if __name__ == '__main__':
    main()
